# E1 — End-to-End Performance: Working Plan & Simplifications

**Purpose:** track what we're running *now* for E1 versus the full E1 spec in
`docs/experiments.md`, so we can come back and re-expand scope deliberately.

---

## Scope for the current run (simplified)

| Axis | Current | Full E1 spec |
|------|---------|--------------|
| Systems | NebulaDB, Full MWJ (SQLite = correctness only) | + Obliviator |
| Dataset | Banking W1 @ 1M acc / 5M txn | + IBM AML-Data W4 (HI-Small) |
| Query shapes | Chain-only: 1/2/3/4/5-hop | + `star4`, `tree` |
| Measurement protocol | TBD when runner lands (likely 1 warm-up + N measurement runs) | median of 5 |

## What we're explicitly deferring (and why)

1. **Obliviator baseline** — defer until NebulaDB + Full MWJ are stable on the
   chain sweep. Re-add by adding a third row of cells to the runner; the
   Obliviator chained 1-hop binary already exists (`obliviator_1hop_chained`)
   and E6 has a working integration path.
2. **IBM AML-Data W4** — defer. Brings in a separate dataset pipeline and
   `aml_*` queries; not blocking the headline figure for Banking.
3. **Star4 and tree queries** — defer. Need to extend
   `scripts/rewrite_chain_query.py` (chain-only today) to handle non-chain
   decomposition before NebulaDB can run those shapes end-to-end.
4. **Final measurement protocol (median of 5)** — defer the decision. Whether
   we do 5 or 3 measurement runs depends on per-cell wall-clock at 1M, which
   we'll see at the smoke step.

## Filter strategy for the chain queries

**Decision:** single-account start filter, no endpoint filter.
`WHERE a1.account_id = 46` on every chain query.

**Why not the existing template** (`a1.owner_id=52 AND a_last.owner_id=45`):
Banking_1M is heavily Zipfian. Only 44K of 1M accounts have any outgoing
txns; median out-degree is 1; a few hub accounts have hundreds of thousands
of out-edges. The forward closure of a random *owner-based* start set
collapses to 0 within 1–2 hops. Endpoint filters on a uniformly-random
ending owner set then make the join output identically zero at every hop
>= 2. The existing `banking_chain4_filtered.sql` (owner 52 -> 45) almost
certainly emits 0 rows on banking_1M.

**Why `account_id = 46`:** picked from a probe of 500 sampled
bidirectional accounts. Yields bounded, monotone, non-empty forward
closures at every hop count:

| hop | output rows (= forward paths of length k from acc 46) |
|----:|-----:|
| 1 | 7,851 |
| 2 | 2,635 |
| 3 | 608 |
| 4 | 78 |
| 5 | 10 |

Alternatives left on the table if 46 is wrong for any reason:
- `account_id = 199` -> `[917, 4656, 840, 116, 7]` (2-hop is largest)
- `account_id = 48`  -> `[7529, 1418, 324, 104, 4]` (similar to 46)

**Caveat:** these row counts are the *final* join output. For NebulaDB the
intermediate one-hop result is independent of the chain length (it's the
same FK join every time); for Full MWJ the per-stage intermediate sizes
depend on the join order chosen by the MWJ planner and can be much larger.
Smoke step will surface OOM/timeout if 5-hop Full MWJ blows up.

## File status

**To write this turn:**
- [ ] `input/queries/banking_1hop.sql`
- [ ] `input/queries/banking_2hop.sql`
- [ ] `input/queries/banking_3hop.sql`
- [ ] `input/queries/banking_4hop.sql`
- [ ] `input/queries/banking_5hop.sql`

**Existing, will be superseded:**
- `input/queries/banking_chain4_filtered.sql` — same 4-hop shape but uses
  the empty-output owner-52/45 filter. Keep for now (no orphaned references
  yet); delete or rewrite once `banking_4hop.sql` is validated.
- `input/queries/banking_chain_filtered.sql` — 3-hop, same dual-owner
  pattern; same treatment.

## Open questions for review (before the runner work)

1. Filter strategy: starting-account-only with no endpoint constraint OK,
   or do you want me to invent a different endpoint shape (e.g.,
   `a_last.balance > X` with X tuned to give ~10–1000 matches per hop)?
2. Pick of `account_id = 46`: anchored to a one-shot Zipfian-aware probe,
   not chosen for any semantic reason. Fine, or want a different one?
3. Do we keep the old `banking_chain4_filtered.sql` around for backward
   reference, or delete it once `banking_4hop.sql` is validated?

## Pipeline (after queries are accepted, in order)

1. **Correctness gate**: each chain query x {NebulaDB, Full MWJ} vs SQLite
   on `banking_1k` (small dataset; recompute starting-account filter for
   that scale).
2. **E1 runner**: `scripts/experiments/run_e1_main.py`, modeled on E6's
   `run_one_hop_thread_scaling.py`. Axes: system x query x repetition.
3. **Smoke at 1M**: 1 sample per cell to flag OOM / timeout before
   committing to median-of-N.
4. **Full sweep + plot**: `results/e1_main/{raw_runs.csv, summary.csv,
   metadata.json, stdout.log}` and a grouped-bar PDF.

## Re-expanding scope later

When we're ready to undo each simplification, the entry points are:
- Add Obliviator -> extend the runner's system loop; reuse the E6 binary path.
- Add W4 -> drop `aml_*` queries + IBM dataset into the same runner;
  parameterize the data path.
- Add star4/tree -> first extend `scripts/rewrite_chain_query.py` (or fork
  it), then add the queries and re-run.

---

## Cleanup after E1 (deferred)

Once the E1 sweep is finalized and the figure is locked, do a cleanup pass
to remove the now-superseded `banking_chain*_filtered.sql` files:

**Step 1 — update or delete references.** These six locations still
mention the old filenames:

| Location | What it does with the name |
|---|---|
| `scripts/run_banking_bench.sh` | uses as input — switch to `banking_{3,4}hop.sql` or delete the script if unused |
| `scripts/benchmark_decomposition.sh` | same |
| `scripts/benchmark_scaling.sh` | same |
| `scripts/plot_scaling_benchmark.py` | same |
| `scripts/run_decomposed_pipeline.sh` | usage string only — update the example |
| `doc/query-decomposition.md` | docs example — update |
| `docs/workloads.md` | "exists as `banking_chain4_filtered.sql`" note — drop once `banking_4hop.sql` is canonical |

**Step 2 — delete the files:**
- `input/queries/banking_chain_filtered.sql` (3-hop, owner 52->45)
- `input/queries/banking_chain4_filtered.sql` (4-hop, owner 52->45)

Defer this entire step until E1 is published — the new queries don't
conflict with the old ones at the filesystem level, so we lose nothing
by waiting and avoid breaking any downstream tooling we forgot about.

---

## Note on NebulaDB at 1-hop

The NebulaDB system has two execution shapes depending on hop count:

- **1-hop**: `banking_onehop` alone. The one-hop binary's output IS the
  1-hop join. There is no rewrite stage and no MWJ invocation.
- **>= 2-hop**: `banking_onehop` -> `rewrite_chain_query` -> `sgx_app`.

The starting-account filter (`a1.account_id = X`) for the 1-hop case is
applied as a post-filter on hop.csv during correctness checking. For
benchmarking (E1 runner) the equivalent is: the one-hop binary emits all
txns, and the wall-clock we report for NebulaDB 1-hop is just the
one-hop binary's runtime (no MWJ stage to add). Applying the filter is
O(|hop|) and doesn't change the oblivious work; we either apply it inside
the binary or report unfiltered timing and note the filter is downstream.
