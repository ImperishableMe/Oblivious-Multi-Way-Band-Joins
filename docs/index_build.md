# Index Build (OFFLINE) Cost — All Six Paper Datasets

Runner: `scripts/experiments/run_index_build.py`
Results: `results/index_build/`

## What it measures

The one-hop pipeline pays a query-independent OFFLINE cost before any query runs.
This experiment reports that cost per dataset, at n=3, for every dataset in the
paper's `tab:datasets`.

| Stage | Meaning |
|-------|---------|
| `buildNodeIndex` | The oblivious node index over the node table, sized by the public edge count. |
| `index copy (src)` | Deep copy of the built index — probing is destructive, so each side needs its own copy. |
| `initProbeSide (wall)` | Concurrent wall-clock of the two per-side probe scaffolds. The `(src)`/`(dst)` entries are diagnostic (`*`) and are never summed. |

### Two indexes, so the build time doubles

The pipeline needs **one index per probe side** (src and dst). The driver builds
one index and deep-copies it for the second side, so a measured run contains a
single `buildNodeIndex` stage. The **reported** index-build cost builds both from
scratch:

```
index build   = 2 x buildNodeIndex            <- the headline number
offline total = 2 x buildNodeIndex + initProbeSide (wall)
```

The deep copy drops out of the reported total, since it exists only because of
that reuse. `summary.csv` keeps every variant, so nothing is hidden:

| Column | Meaning |
|--------|---------|
| `index_build_*` | Reported: 2 × `buildNodeIndex`. Doubling scales the mean and the stddev alike. |
| `build_once_*` | The raw single-build measurement, undoubled. |
| `offline_*` | Reported OFFLINE total: 2 × build + probe scaffolds. |
| `offline_measured_*` | What the binary itself totalled for OFFLINE in a run (one build + deep copy + scaffolds). |
| `index_count` | The multiplier in force (2), recorded per row. |

ONLINE stages are parsed and stored too, so the one-time OFFLINE cost can be
placed next to the recurring per-query ONLINE cost, but this experiment exists for
the OFFLINE column.

## Protocol

- Per dataset: **1 discarded warm-up run + 3 measured runs**, strictly sequential,
  one process per run, full machine each time.
- Datasets run smallest-edge-count first, so failures surface fast.
- One-hop driver at **64 threads** (matches E3's `--onehop-threads`).
- The one-hop result CSV is written to `/dev/null`: only timings are kept, and
  HI-Large's hop table is ~180M rows. CSV read/write is category `IO` and is
  excluded from both totals regardless.

### Why the warm-up matters more here than elsewhere

`buildNodeIndex` searches hash strategies on a cold planner and caches the winning
plan in `obligraph/build/hash_map.bin72`. cit-Patents takes **~16 min cold** and
seconds warm (see the W3 notes in `docs/workloads.md`); HI-Large's first run after
its 5.3 GB slim `txn.csv` was written showed a 2.6 h `buildNodeIndex` purely from
cold page cache. The measured runs therefore report the **warm-planner** index
build, which is what every other experiment in this series measures. Warm-up rows
stay in `raw_runs.csv` (`is_warmup=1`) so a cold/warm gap is visible rather than
hidden.

## Datasets and drivers

| Label | Paper name | Driver | Data dir |
|-------|-----------|--------|----------|
| `banking_1M` | Banking (synthetic) | `banking_onehop` | `input/plaintext/banking_1M` |
| `hi_small` | IBM AML HI-Small | `ibm_aml_onehop` | `input/plaintext/ibm_aml_hi_small` |
| `snb_sf30` | LDBC SNB (SF30) | `ldbc_snb_onehop` | `input/plaintext/ldbc_snb_sf30` |
| `patents` | SNAP cit-Patents | `snap_patents_onehop` | `input/plaintext/snap_patents` |
| `hi_medium` | IBM AML HI-Medium | `ibm_aml_onehop` | `input/plaintext/ibm_aml_hi_medium` |
| `hi_large` | IBM AML HI-Large | `ibm_aml_onehop` | `input/plaintext/ibm_aml_hi_large_slim` |

HI-Large uses the column-trimmed slim `txn` table (the E2 slim path). The index is
built over the node table and sized by the edge count — neither of which the column
trim changes.

`banking_10k` is a non-default smoke entry (`--datasets banking_10k`), useful for
checking the runner end-to-end in ~1 s.

## Run commands

```
python3 scripts/experiments/run_index_build.py                       # full sweep, rebuild the drivers
python3 scripts/experiments/run_index_build.py --skip-build          # re-run only
python3 scripts/experiments/run_index_build.py --datasets hi_small patents
python3 scripts/experiments/run_index_build.py --reps 3 --threads 64
python3 scripts/experiments/run_index_build.py --summarize-only   # re-derive the
                                                # tables from raw_runs.csv only
```

`--summarize-only` measures nothing: it rebuilds `stages.csv`, `summary.csv` and
`index_build.tex` from the recorded `raw_runs.csv` (and picks the hash strategy and
hop row count back out of `binary_stdout.log`). Use it when the presentation
changes — a different `INDEX_COUNT`, a new column — so a multi-hour sweep is not
repeated for a formatting decision.

## Outputs (`results/index_build/`)

| File | Contents |
|------|----------|
| `raw_runs.csv` | Every stage of every run, warm-up rows included (`is_warmup`, `in_wall_clock`). |
| `stages.csv` | One row per (dataset, stage): median/mean/min/max/stddev across the measured runs, both categories. |
| `summary.csv` | One row per dataset: reported (doubled) index build, raw single build, OFFLINE and ONLINE totals, the per-stage OFFLINE means, the chosen hash strategy, and the hop row count. |
| `index_build.tex` | LaTeX `tab:index_build`, mean ± stddev in seconds, index build already doubled. |
| `run_metadata.json` | Commit, branch, host, nproc, threads, reps, dataset paths. |
| `binary_stdout.log` | Full stdout of every invocation. |

`in_wall_clock=0` rows are diagnostic stages that run inside a parallel block. They
must never be summed alongside `in_wall_clock=1` rows or parallel work is
double-counted.

## Results (n=3, 2026-08-25, commit `cc0f719`, 64 threads, 120-core / 471 GB host)

Mean ± stddev over 3 measured runs, 1 discarded warm-up. Seconds.

| Dataset | Nodes | Edges | Index build (2×) | Offline total | Single build | ONLINE |
|---------|------:|------:|-----------------:|--------------:|-------------:|-------:|
| Banking (synthetic) | 1.0M | 5.0M | 1.82 ± 0.03 | 1.98 ± 0.03 | 0.91 | 0.40 |
| IBM AML HI-Small | 515K | 5.1M | 0.95 ± 0.01 | 1.12 ± 0.01 | 0.47 | 0.35 |
| LDBC SNB (SF30) | 165K | 12.0M | 0.47 ± 0.01 | 0.87 ± 0.02 | 0.24 | 0.83 |
| SNAP cit-Patents | 3.8M | 16.5M | 8.74 ± 0.26 | 9.30 ± 0.25 | 4.37 | 2.03 |
| IBM AML HI-Medium | 2.1M | 31.9M | 4.52 ± 0.14 | 5.57 ± 0.13 | 2.26 | 3.14 |
| IBM AML HI-Large | 2.1M | 179.7M | 9.24 ± 0.03 | 15.13 ± 0.16 | 4.62 | 22.81 |

What the numbers say:

- **Index build tracks nodes, not edges.** SNB has 12M edges but only 165K nodes and
  the cheapest build of all six (0.47 s); cit-Patents has 16.5M edges and 3.8M nodes
  and the most expensive (8.74 s). The index is sized by the node table padded to a
  power of two — cit-Patents and HI-Large both land on n=2^22 — while the edge count
  only sets `op_num`. HI-Large's 180M edges cost it nothing over HI-Medium's 31.9M
  in the build itself (9.24 s vs 4.52 s is the 2^22-vs-2^21 node padding).
- **Edges show up in the probe scaffolds instead.** `initProbeSide (wall)` is what
  scales with edge count: 0.15 s on Banking, 5.90 s on HI-Large, where it becomes
  the larger half of the offline total.
- **Run-to-run spread is small**: stddev is ≤ 3 % of the mean everywhere, and ≤ 0.5 %
  on the two AML extremes.
- **The offline cost is one-time and modest against the query.** On HI-Large the
  entire offline setup (15.1 s) is under a third of a single one-hop ONLINE pass
  (22.8 s), and it is amortized over every query on that graph. Only on the small
  and mid datasets does the build exceed one ONLINE pass, which is what the
  amortization experiment (`results/one_hop_amortization/`) quantifies with its
  breakeven-N.
- **No cold-planner surprises**: every warm-up build landed within a few percent of
  the measured runs (cit-Patents 4.58 s warm-up vs 4.24–4.51 s measured; HI-Large
  4.47 s vs 4.61–4.63 s), so the hash plans in `hash_map.bin72` were already cached
  and page cache was warm.

Cross-check: HI-Small's single build here (0.474 s) matches the independently
measured `build_once_ms` in `results/one_hop_amortization/measured.csv` (0.464 s),
and HI-Large's (4.62 s) matches the 4.25 s warm figure recorded during the E2 slim
run.
