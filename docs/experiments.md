# Phase 2: Experiment Design for NebulaDB Evaluation

## Context

This document defines the experiments for a SIGMOD/VLDB submission. Primary workload: Banking W1 (defined in `docs/workloads.md`). Secondary workload: IBM AML-Data W4 for cross-dataset validation and scaling beyond 10M edges.

**Systems under comparison:**
- **NebulaDB** — decomposed execution: ForwardFill one-hop + reduced multi-way band join (MWJ)
- **Full MWJ** — non-decomposed: entire query handled by oblivious multi-way band join
- **Obliviator** — most performant oblivious join system; executes multi-way joins as sequential pairwise joins, allows intermediate result leakage (already set up)
- **Non-oblivious (SQLite)** — reference implementation used for **correctness only**, not performance comparison. We anticipate a large performance gap; keeping SQLite in the pipeline lets us validate that every system produces the same join output

**Metric (all experiments):**
- **Latency** (median of 5 runs; warm-up handling TBD)

**Scale target:** up to 10M edges on Banking W1; extended to 32M+ (HI-Medium) and optionally 180M edges (HI-Large) on IBM AML-Data W4.

---

## E1: End-to-End Performance (Main Result)

**Goal**: Headline comparison — NebulaDB vs baselines across query shapes, on two workloads for cross-dataset validation.

**This is the most important figure in the paper.**

| Parameter | Value |
|-----------|-------|
| Systems | NebulaDB, Full MWJ, Obliviator (SQLite for correctness only) |
| Datasets | **Banking W1**: 1M accounts / 5M txns. **IBM AML-Data W4**: HI-Small (515K / 5M) |
| Queries (W1) | 1/2/3/4/5-hop chains; star4; tree |
| Queries (W4) | `aml_1hop` … `aml_5hop`; `aml_fanout`; `aml_fanin`; `aml_tree` |
| Metric | Latency (median of 5 runs) |

**Presentation**: Grouped bar chart — one group per query shape, bars for each system. Two panels (W1 and W4) side-by-side, or one chart per dataset.

**Expected story**:
- Among oblivious systems, NebulaDB is competitive with Obliviator despite stronger security (fully oblivious, no intermediate leakage)
- NebulaDB is ~1.5–2× faster than Full MWJ across query shapes
- Full MWJ is the slowest oblivious variant (no decomposition)
- Speedup grows with hop count (more hops = more tables eliminated by decomposition)
- W4 reproduces the same system ordering as W1 (generalization across workloads)

---

## E2: Scalability with Data Size

**Goal**: Show scaling from small graphs to 10M edges on Banking, and further to 32M+ on IBM AML-Data.

| Parameter | Value |
|-----------|-------|
| Systems | NebulaDB, Full MWJ, Obliviator |
| Query | 3-hop chain (fixed) |
| Datasets | **Banking W1** at 50K, 100K, 500K, 1M, 5M, 10M edges. **IBM AML-Data W4** at HI-Small (5M), HI-Medium (32M), optionally HI-Large (180M) |
| Edge-to-node ratio (W1) | 5× (fixed) |
| Metric | Latency |

**Presentation**: Log-log line plot. One line per system. X-axis = number of edges; Y-axis = latency. W1 and W4 points co-plotted (distinguish workload via marker style), or two subplots.

**Expected story**:
- All systems scale super-linearly (oblivious sort is O(N log N))
- NebulaDB's speedup over Full MWJ is stable or grows with scale
- W4 extends the curve past 10M edges and shows the trend continues
- Obliviator expected faster than NebulaDB at all scales (intermediate-leakage advantage)

---

## E3: Execution Time Breakdown

**Goal**: Attribute time inside NebulaDB — argue the win comes from a *smaller* MWJ, not "ForwardFill dominates total work."

| Parameter | Value |
|-----------|-------|
| System | NebulaDB only |
| Phases | Oblivious sort, online probe, offline build, reduced MWJ |
| Queries | 2-hop, 3-hop, 4-hop chains |
| Data sizes | Banking W1: 100K / 500K / 1M edges |
| Metric | Latency per phase |

**Planning cost** is kept **outside** the reported breakdown — treated as a static one-time lookup, not per-query work.

**Presentation**: Table (one row per (query, data_size); columns for each phase's time + total).

**Expected story**:
- ForwardFill (sort + probe + build) is *not* the hero — the reduced MWJ joins fewer tables
- Reduced MWJ dominates total time (>90% in most configs)
- Oblivious sort is the largest ForwardFill sub-phase

---

## E4: Thread Scalability

**Goal**: End-to-end parallelism. Measure how one-hop and full NebulaDB each scale with cores, and where the single-threaded MWJ caps out.

| Parameter | Value |
|-----------|-------|
| Systems | One-hop only AND full NebulaDB pipeline |
| Threads | 1, 2, 4, 16, 32, 64, 128 |
| Query | 3-hop chain (fixed) |
| Dataset | Banking W1: 1M edges |
| Metric | Latency |

**Presentation**: Line plot. X-axis = thread count; Y-axis = latency. One line per system (one-hop, full NebulaDB).

**Expected story**:
- One-hop scales with threads (oblivious sort + probe are parallel)
- Full NebulaDB: MWJ is single-threaded, so the full pipeline's scalability curve **flattens** beyond the point where the MWJ dominates — only the one-hop phase benefits from more cores

**Note**: partial one-hop-only results exist in `obligraph/benchmarks/banking_onehop_results.md`. Extend with the full pipeline.

---

## E5: Online / Offline Split

**Goal**: Report online vs offline work separately for the one-hop component. Stress the two-tier oblivious bucket hashmap whose parameters scale independently with |V| and |E|.

*Status: must-run, pending final dataset generator configuration.*

| Parameter | Value |
|-----------|-------|
| System | One-hop only (not full NebulaDB) |
| Edges | 1M (fixed) |
| Node counts | 10K, 50K, 100K, 200K, 500K |
| Edge-to-node ratios | 100:1, 20:1, 10:1, 5:1, 2:1 |
| Query | 3-hop chain |
| Dataset | Banking W1 (generated at each ratio) |
| Metric | Latency (online reported primarily; offline reported separately) |

**Presentation**: Line plot (ratio or |V| vs latency), or a stacked bar showing online vs offline at each ratio.

**Expected story**: *TBD* — write after seeing the data. The two-tier hashmap sizes buckets based on |V| and |E| independently; we expect the online/offline trade-off to shift as the ratio changes.

**Implementation note**: requires a Banking data generator that supports configurable node count with fixed edge count.

---

## Experiment Priority and Dependencies

| Priority | Experiment | Blocking? | Depends on |
|----------|-----------|-----------|------------|
| P0 | E1 (main result) | Yes — main figure | W1 + W4 data; NebulaDB / Full MWJ / Obliviator ready |
| P0 | E2 (data scaling) | Yes — scalability story | W1 data at all scales; W4 HI-Small / HI-Medium |
| P1 | E3 (breakdown) | Yes — justifies design | Per-phase timing instrumentation (in place) |
| P1 | E4 (threads) | Yes — parallelism | Partial one-hop results exist |
| P1 | E5 (online/offline) | Must-run | Dataset generator with configurable |V| / |E| |

### System Dependencies

Each system must pass a correctness check before experiments:
1. **NebulaDB**: decomposed pipeline output matches Full MWJ output
2. **Full MWJ**: existing system, validated
3. **Obliviator**: same query format, produces same results
4. **SQLite**: reference; cross-validates the other three on every query

---

## Benchmarking Infrastructure

### Runner Script (`scripts/run_experiment.sh`)

Unified runner:
1. Takes parameters: system, query, dataset, num_runs, output_dir
2. Runs warm-up (TBD: 0 or 1 discarded run)
3. Runs 5 measurement runs
4. Records per-run: latency, output row count
5. Computes: median, std dev
6. Outputs to CSV: `results/{experiment}/{system}_{query}_{dataset}.csv`

### Output Format

Per-run:
```
system,query,dataset,run_id,latency_ms,output_rows
```

Summary (one row per config):
```
system,query,dataset,median_ms,stddev_ms
```

### Plotting Scripts (`scripts/plot_experiments.py`)

One function per experiment figure:
- `plot_e1_main_result()` — grouped bar chart (per dataset)
- `plot_e2_scaling()` — log-log line plot
- `plot_e3_breakdown()` — table / stacked bar
- `plot_e4_threads()` — line plot (one-hop vs full NebulaDB)
- `plot_e5_online_offline()` — line plot or stacked bar

Output: PDF figures for direct inclusion in LaTeX via `\includegraphics`.

---

## Expected Paper Figures

| Figure | Type | Experiment | Size |
|--------|------|-----------|------|
| Fig 5 | Grouped bar (2 panels) | E1: Main result (W1 and W4) | Full width |
| Fig 6 | Log-log lines | E2: Data scaling | Half width |
| Fig 7 | Table or stacked bar | E3: Breakdown | Half width |
| Fig 8 | Line plot | E4: Thread scalability | Half width |
| Fig 9 | Line plot | E5: Online/Offline | Half width |

Total: 5 figures.

---

## Reproducibility Checklist

For each experiment run, record:
- [ ] Git commit hash
- [ ] Compilation flags (DEBUG, SLIM_ENTRY, TRACE_OPS, optimization level)
- [ ] Hardware: CPU model, core count, RAM, OS version
- [ ] Thread count (if applicable)
- [ ] Exact command used
- [ ] Dataset path and row counts (verified)
- [ ] All raw CSV outputs preserved in `results/` directory
