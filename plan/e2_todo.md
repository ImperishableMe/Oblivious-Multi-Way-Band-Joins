# E2 — Scalability with Data Size: Working Plan & Simplifications

**Purpose:** track what we're running *now* for E2 versus the full E2 spec in
`docs/experiments.md`, so we can come back and re-expand scope deliberately.

---

## Scope for the current run (simplified)

| Axis | Current | Full E2 spec |
|------|---------|--------------|
| Systems | NebulaDB, Full MWJ (+ Full MWJ no-filter, opt-in) | + Obliviator |
| Dataset | Banking W1 only (seed=42, edges = 5× accounts) | + IBM AML-Data W4: HI-Small (5M), HI-Medium (32M), optionally HI-Large (180M) |
| Sizes | 50K / 100K / 500K / 1M / 5M / 10M edges (= 10K / 20K / 100K / 200K / 1M / 2M accounts) | same Banking points, plus the three W4 sizes |
| Query | One fixed chain — `banking_3hop` by default, swap to `banking_2hop` via `--query` if 3-hop blows up at the top end | 3-hop chain (fixed) |
| Full MWJ ceiling | Skip Full MWJ for edges > 1M (i.e. only 50K / 100K / 500K / 1M) | Run at every size (no cap) |
| Measurement protocol | 1 warm-up + 1 measurement (both CLI-configurable) | Median of 5 |

## What we're explicitly deferring (and why)

1. **Obliviator baseline** — defer until NebulaDB + Full MWJ are stable on the
   sweep. Re-add by extending the runner's system loop; the chained driver
   already integrated in E6 (`obliviator_1hop_chained`) gives the one-hop
   side, and the >=2-hop continuation is the design open question we punt on.
2. **IBM AML-Data W4** — defer. Brings in a separate dataset pipeline and
   `aml_*` queries; not blocking the Banking scaling curve.
3. **Full MWJ at 5M / 10M edges** — defer. Expected to be impractical
   (super-linear oblivious sort over the full input). The curve truncates at
   1M; NebulaDB extends past that to show the scaling story.
4. **Median-of-5 protocol** — defer. Decide after the first full sweep at 1+1
   how stable per-cell measurements are at the largest size.
5. **Plot / figure** — defer. Pick representation (log-log line, possibly
   double y-axis with output rows) after seeing the data.
6. **2-hop fallback** — not a deferral; it's a CLI affordance. If 3-hop at
   10M is intractable on this machine, re-run the same script with
   `--query banking_2hop` and report that curve instead. The runner only
   supports one query per invocation by design.

## Filter strategy

Same as E1: every Banking chain query carries `WHERE a1.account_id = 46`.
Rationale (Zipfian skew, monotone non-empty forward closure) is documented in
`plan/e1_todo.md` and the queries themselves; nothing E2-specific.

**Caveat at scale:** `account_id = 46` was picked on `banking_1M`. At other
sizes the same row count probe wasn't repeated; the filter still works
mechanically (it's just an equality on the start node), but the forward
closure size at 10M / 50K is whatever it turns out to be. Recorded as
`output_rows` per run; no cross-size correctness check.

**No-filter baseline (`full_mwj_no_filter`, opt-in):** runs `sgx_app
--no-filter`, dropping the WHERE-clause selection so the *full unfiltered*
multi-way join is computed. Output explodes with both hop count and dataset
size, so it OOMs at the larger sizes by design. It is gated by the same
`--full-mwj-max-edges` cap as `full_mwj`, and the runner is OOM-tolerant: a
binary OOM/crash is recorded per cell (`output_rows=OOM`, empty `total_ms`)
and larger sizes for that system are then `SKIPPED` — the sweep never aborts.
Not in the default system set; add it explicitly via `--systems`.

## Dataset auto-generation

The runner regenerates a `banking_<N>` directory under `input/plaintext/`
when its row counts don't match what seed=42 produces (same row-count check
as `scripts/run_onehop_scaling.py`). Skippable with `--skip-generation`.

Existing on disk (per project state at planning time):
`banking_1M`, `banking_200k`, `banking_500k` — others (`banking_10k`,
`banking_20k`, `banking_100k`, `banking_2M`) will be generated on first run.

## Output layout (mirrors E1)

Under `results/e2_scaling/`:

- `raw_runs.csv` — every run incl. warm-ups. Columns:
  `system, query, dataset, num_accounts, num_edges, run_id, is_warmup,
   total_ms, onehop_ms, mwj_ms, output_rows`.
- `summary.csv` — measurement runs only, per cell:
  `system, query, dataset, num_accounts, num_edges, n_runs,
   median_total_ms, median_onehop_ms, median_mwj_ms,
   min_total_ms, max_total_ms, stddev_total_ms, output_rows`.
  (Components reported separately as well as the total, per user spec.)
- `decomposed/<query>.sql` — cached rewriter output for NebulaDB.
- `run_metadata.json` — commit, host, nproc, args, sizes-run.
- `binary_stdout.log` — every binary invocation.

**Per-run binary output (`hop.csv`, sgx_app result CSV):** lives in a
`tempfile.TemporaryDirectory()` for the duration of one rep and is discarded
after. We do **not** persist query results from experiment runs — see
`memory/feedback_experiment_outputs.md`.

## Pipeline

1. **Runner**: `scripts/experiments/run_e2_scaling.py`, modeled on
   `run_e1_main.py`. Axes: rep (outer) × size (inner) × system (innermost).
   Strictly sequential, full machine per run.
2. **Smoke** at 200k / 1M before committing to the full sweep — quickest way
   to flag a mistuned filter, a Full MWJ blowup mid-cap, or a parse error.
3. **Full sweep**: `results/e2_scaling/{raw_runs.csv, summary.csv,
   run_metadata.json, binary_stdout.log}`.
4. **Plot** — TBD after data lands.

## Re-expanding scope later

When we're ready to undo each simplification, the entry points are:
- **Add Obliviator** — extend the runner's system loop with a third branch
  that calls the Obliviator chained 1-hop binary for 1-hop, and (still open
  design) a multi-pairwise composition for 2-hop / 3-hop.
- **Lift the Full MWJ cap** — remove the `--full-mwj-max-edges` guard once
  the machine can afford the wall-clock.
- **Add W4** — parameterize `DATA_ROOT` / `--dataset-family` and reuse the
  same axes; W4 needs its own size table and its own `aml_3hop` query.
- **Multiple queries per invocation** — switch `--query` from single value
  to a comma list and re-introduce an outer query loop, like E1's runner.
- **Median-of-5** — set `--measurement-runs 5` at invocation time; the
  summary already aggregates with median / min / max / stddev.
