# Phase 2: Experiment Design for NebulaDB Evaluation

## Context

This document defines the experiments for a SIGMOD/VLDB submission. Primary workload: Banking W1 (defined in `docs/workloads.md`).

**Systems under comparison:**
- **NebulaDB** — decomposed execution: ForwardFill one-hop + reduced multi-way band join (MWJ)
- **Full MWJ** — non-decomposed: entire query handled by oblivious multi-way band join
- **Obliviator** — most performant oblivious join system; executes multi-way joins as sequential pairwise joins, allows intermediate result leakage (published, already set up)
- **Non-oblivious** — our codebase with standard (non-oblivious) sort/join primitives

**Metric (all experiments):**
- **Wall-clock time** (median of 5 runs, first run discarded as warm-up)

**Scale target:** Up to 10M edges.

---

## E1: End-to-End Performance (Main Result)

**Goal**: Headline comparison — NebulaDB vs all baselines across query shapes.

**This is the most important figure in the paper.**

| Parameter | Value |
|-----------|-------|
| Systems | NebulaDB, Full MWJ, Obliviator, Non-oblivious |
| Dataset | Banking W1: 50K accounts / 250K txns |
| Queries | 1-hop, 2-hop, 3-hop, 4-hop, 5-hop chain; 3-branch star; tree pattern; heterogeneous chain (nice-to-have) |
| Metrics | Wall-clock time |
| Runs | 6 per config (drop first, report median of remaining 5) |

**Presentation**: Grouped bar chart — one group per query shape, bars for each system.

**Expected story**:
- Non-oblivious is fastest (no obliviousness cost) — this is the "price of security"
- Obliviator is fastest among oblivious systems (allows intermediate result leakage — weaker security guarantee)
- NebulaDB is competitive with Obliviator despite stronger security (fully oblivious, no intermediate leakage)
- NebulaDB is 1.5-2x faster than Full MWJ across all query shapes
- Full MWJ is slowest oblivious system (no decomposition)
- Speedup grows with hop count (more hops = more tables eliminated by decomposition)

**Query files needed** (Banking schema):
```
input/queries/banking_1hop.sql    -- (a1)-[t]->(a2) WHERE a1.balance > X
input/queries/banking_2hop.sql    -- (a1)-[t1]->(a2)-[t2]->(a3)
input/queries/banking_3hop.sql    -- 3-hop chain
input/queries/banking_4hop.sql    -- 4-hop chain (exists: banking_chain4_filtered.sql)
input/queries/banking_5hop.sql    -- 5-hop chain
input/queries/banking_star3.sql   -- (a1)-[t1]->(a2), (a1)-[t2]->(a3), (a1)-[t3]->(a4)
input/queries/banking_tree.sql    -- (a1)-[t1]->(a2)-[t2]->(a3), (a2)-[t3]->(a4)

# Nice-to-have: heterogeneous edge-label chain (different edge tables per hop with skewed cardinalities)
# input/queries/banking_hetero.sql -- e.g., Account-[Txn]->Merchant-[Payment]->Device style chain
```

---

## E2: Scalability with Data Size

**Goal**: Show NebulaDB scales gracefully from small to 10M edges.

| Parameter | Value |
|-----------|-------|
| Systems | NebulaDB, Full MWJ, Obliviator, Non-oblivious |
| Query | 3-hop chain (fixed) |
| Dataset | Banking W1, varying size |
| Edge counts | 50K, 100K, 500K, 1M, 5M, 10M |
| Edge-to-node ratio | 5x (fixed) |
| Metrics | Wall-clock time |

**Presentation**: Log-log line plot. One line per system. X-axis = number of edges, Y-axis = time.

**Expected story**:
- All systems scale super-linearly (oblivious sort is O(N log N))
- NebulaDB's speedup over Full MWJ is consistent or grows with scale
- Obliviator expected to be faster than NebulaDB at all scales (intermediate leakage advantage)

---

## E3: Scalability with Query Complexity (Hops)

**Goal**: Show decomposition benefit increases with query complexity.

| Parameter | Value |
|-----------|-------|
| Systems | NebulaDB, Full MWJ |
| Queries | 1-hop, 2-hop, 3-hop, 4-hop, 5-hop, 6-hop chains |
| Dataset | Banking W1: 20K accounts / 100K txns (fixed) |
| Metrics | Wall-clock time |

**Presentation**: Line plot. X-axis = number of hops. Y-axis = wall-clock time. One line per system.

**Expected story**:
- At 1-hop: NebulaDB replaces the entire 3-way MWJ with ForwardFill (maximum relative benefit)
- At k-hops: Full MWJ does a (2k+1)-way join; NebulaDB does k one-hops + k-way join
- Absolute speedup grows because the MWJ cost grows faster with more input tables
- Relative speedup may plateau since both ForwardFill cost and reduced MWJ cost grow

**Key data point**: At what hop count does decomposition stop helping? (If it does — this is interesting either way.)

---

## E4: Execution Time Breakdown

**Goal**: Show where time is spent. Justify that ForwardFill is cheap — the win comes from the reduced MWJ.

| Parameter | Value |
|-----------|-------|
| System | NebulaDB (decomposed) only |
| Phases to measure | (1) Hash map build, (2) Oblivious sort, (3) Duplicate suppression, (4) Hash probe, (5) Forward-fill, (6) Compaction, (7) Reduced MWJ |
| Queries | 2-hop, 3-hop, 4-hop chains; heterogeneous chain (nice-to-have) |
| Data sizes | 100K, 500K, 1M edges |
| Metrics | Time per phase |

**Presentation**: Stacked bar chart. One bar per (query, data_size) pair. Colors = phases.

**Expected story**:
- ForwardFill phases (1-6 combined) are <5% of total time
- Reduced MWJ dominates (>90% of time)
- Oblivious sort is the most expensive ForwardFill sub-phase
- The benefit isn't that ForwardFill is fast — it's that the *reduced* MWJ is faster than the *full* MWJ because it joins fewer tables

**Implementation note**: Requires timing instrumentation at each phase boundary. Check if existing timing infrastructure captures these phases or needs extension.

---

## E5: Impact of Edge-to-Node Ratio

**Goal**: Measure how the edge-to-node ratio affects performance, driven by the oblivious two-tier bucket hashmap whose parameters are sized based on #nodes and #edges independently.

| Parameter | Value |
|-----------|-------|
| System | NebulaDB only |
| Edges | 1M (fixed) |
| Node counts | 10K, 50K, 100K, 200K, 500K |
| Ratios | 100:1, 20:1, 10:1, 5:1, 2:1 |
| Query | 3-hop chain (fixed) |
| Dataset | Banking W1 (generated at each ratio) |
| Metrics | Wall-clock time |
| Runs | 6 per config (drop first, report median of remaining 5) |

**Presentation**: Line plot. X-axis = edge-to-node ratio (or node count). Y-axis = wall-clock time.

**Expected story**:
- The oblivious two-tier bucket hashmap sizes its parameters based on #nodes and #edges independently
- More nodes (lower ratio) → larger hashmap buckets → different performance profile
- Fewer nodes (higher ratio) → more duplicate keys → hashmap behaves differently
- Shows how the system's core data structure responds to varying graph density

**Implementation note**: Requires a Banking data generator that supports configurable node count with fixed edge count.

---

## E6: Thread Scalability

**Goal**: Show parallel scaling of the full pipeline.

| Parameter | Value |
|-----------|-------|
| System | NebulaDB |
| Threads | 1, 2, 4, 8, 16, 32, 64 |
| Fix | Banking W1, 1M edges, 3-hop chain |
| Metrics | Wall-clock time |

**Presentation**: Line plot. X-axis = thread count. Y-axis = wall-clock time.

**Expected story**:
- Time decreases with more threads up to some point (16-32 threads based on prior results)
- Identify bottleneck: is it oblivious sort (good parallelism) or hash probe (potential contention)?
- Prior experiments showed 16 threads best for small data, 64 for large — reproduce and extend

**Note**: Partial results exist from `obligraph/benchmarks/banking_onehop_results.md`. Extend with full pipeline (not just one-hop).

---

## E7: Cost of Obliviousness

**Goal**: Quantify the overhead of being oblivious vs plaintext execution.

| Parameter | Value |
|-----------|-------|
| Systems | NebulaDB, Full MWJ, Obliviator, Non-oblivious |
| Queries | 2-hop, 3-hop, 4-hop chains |
| Dataset | Banking W1: 100K edges, 500K edges |
| Metrics | Slowdown factor = oblivious_time / non_oblivious_time |

**Presentation**: Table.

| Query | Scale | Non-obliv | NebulaDB | Full MWJ | Obliviator |
|-------|-------|-----------|----------|----------|------------|
| 2-hop | 100K | 1.0x (ref) | ?x | ?x | ?x |
| 2-hop | 500K | 1.0x | ?x | ?x | ?x |
| 3-hop | 100K | 1.0x | ?x | ?x | ?x |
| ... | ... | ... | ... | ... | ... |

**Expected story**:
- Obliviator overhead: lowest among oblivious systems (leaks intermediate results for better performance)
- NebulaDB overhead: 3-10x over non-oblivious (acceptable for full security guarantee)
- Full MWJ overhead: 5-15x (higher than NebulaDB)
- **Key message**: NebulaDB provides the strongest security (no intermediate leakage) at competitive performance — close to Obliviator despite stronger guarantees, and 30-50% faster than Full MWJ

---

## Experiment Priority and Dependencies

### Priority Order (run in this order)

| Priority | Experiment | Blocking? | Depends on |
|----------|-----------|-----------|------------|
| P0 | E1 (main result) | Yes — main figure | W1 data + all 4 systems working |
| P0 | E2 (data scaling) | Yes — scalability story | W1 data at all scales |
| P0 | E3 (hop scaling) | Yes — key insight | W1 data + chain queries 1-6 hop |
| P1 | E4 (breakdown) | Yes — justifies design | Timing instrumentation in NebulaDB |
| P1 | E5 (edge-to-node ratio) | Yes — hashmap analysis | Banking generator with configurable ratio |
| P1 | E6 (threads) | Yes — parallelism | Partial results exist |
| P1 | E7 (obliviousness cost) | Yes — positioning | All 4 systems |

### System Dependencies

Before running experiments, each system must pass a correctness check:
1. **NebulaDB**: Decomposed pipeline produces correct output (matches Full MWJ output)
2. **Full MWJ**: Existing system, already validated
3. **Obliviator**: Can run the same query format, produces same results
4. **Non-oblivious**: Our code with oblivious primitives replaced by standard ones, produces same results

---

## Benchmarking Infrastructure

### Runner Script (`scripts/run_experiment.sh`)

Unified experiment runner that:
1. Takes parameters: system, query, dataset, num_runs, output_dir
2. Runs warm-up (1 discarded run)
3. Runs N measurement runs
4. Records per-run: wall-clock time, output row count
5. Computes: median time, std dev
6. Outputs to CSV: `results/{experiment}/{system}_{query}_{dataset}.csv`

### Output Format

Each CSV row:
```
system,query,dataset,run_id,wall_time_ms,output_rows
```

Summary CSV (one row per config):
```
system,query,dataset,median_time_ms,stddev_ms
```

### Plotting Scripts (`scripts/plot_experiments.py`)

One function per experiment figure:
- `plot_e1_main_result()` — grouped bar chart
- `plot_e2_scaling()` — log-log line plot
- `plot_e3_hops()` — line plot
- `plot_e4_breakdown()` — stacked bar chart
- `plot_e5_ratio()` — line plot (ratio vs time)
- `plot_e6_threads()` — line plot (threads vs time)

Output: PDF figures for direct inclusion in LaTeX via `\includegraphics`.

---

## Expected Paper Figures

| Figure | Type | Experiment | Size |
|--------|------|-----------|------|
| Fig 5 | Grouped bar | E1: Main result | Full width |
| Fig 6 | Log-log lines | E2: Data scaling | Half width |
| Fig 7 | Line plot | E3: Hop scaling | Half width |
| Fig 8 | Stacked bars | E4: Breakdown | Half width |
| Fig 9 | Line plot | E5: Edge-to-node ratio | Half width |
| Fig 10 | Line plot | E6: Thread scalability | Half width |
| Table 2 | Table | E7: Obliviousness cost | - |

Total: 6 figures + 1 table.

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
