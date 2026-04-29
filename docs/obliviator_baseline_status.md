# Obliviator Baseline — Build Status

Snapshot of the Obliviator multi-way driver built on `obl-radix/baselines/obliviatorNFK-TDX/`.
Refer to `docs/obliviator_multiway_design.md` for the full Q&A decision log.

## Decisions taken

### Threat model and semantics
- Intermediate pairwise-join sizes are **allowed to leak** — the one extra leakage Obliviator gets in exchange for sequential pairwise execution.
- Single-table filters are deferred to the **final** join output only — never pushed into CSV loaders or applied between steps.
- Final-stage filter is **plain (non-oblivious) row drop** — sound because the final output size already leaks.
- Between-step projection is **position-based only** (column indices fixed by the query, never by row values).

### Scope
- **Chain queries first** (1-hop … 5-hop); bushy shapes (star/fanout/fanin/tree) deferred.
- **Banking W1 first**; AML W4 in a second pass.
- **Hand-written plans first**, SQL-driven planner deferred.

### Plan format and data flow
- **Text plan file**, kernel-agnostic, with `num_steps` / `step` / `final_filter` / `schema` directives.
- **Side files** are one-side Obliviator text streams (`<row_count>` header + `<key> <data>` lines), produced by per-workload Python converters.
- **Side-file dedup**: one `account.txt` and one `txn.txt` per workload; the plan references them across multiple steps.
- **Intermediates pass between steps in memory** as `elem_t[]` pairs — no text or disk round-trip.
- **Next-step join key** is pulled from a fixed column index in the comma-separated `L.data + "," + R.data` concat.
- **Schema order preserved** as `prior-columns + new-columns` at each concat.
- **All columns carried forward** in the MVP (no projection between steps).

### Kernel selection
- **NFK kernel for every step** — not a per-step dispatch between FK and NFK.
- Reasons: incompatible `elem_t` layouts and colliding symbol names; NFK is strictly more general and degenerates correctly on FK-semantic joins; mixed dispatch would only save ~10–15% over NFK-everywhere.
- **FK driver kept in-tree** as reference; not used by the multi-way paper baseline.

### Per-element layout
- `DATA_LENGTH = 640` bytes (sized from AML 5-hop's 623-byte peak), compile-time overridable via `-DDATA_LENGTH=N`.
- NFK's `int key` retained — workload IDs all fit in int32; widening deferred.

### Timing contract
- **Counts**: kernel time per step + in-memory merge/concat/key-pull + final filter pass.
- **Excluded**: initial CSV read, final CSV write.
- **No intermediate ever hits disk.**
- **Per-step breakdown** reported: sort, join_total, merge, build_side0; plus grand totals.

### Build and vendoring
- **Source**: `github.com/dsg-uwaterloo/obl-radix/tree/main/baselines/{obliviatorFK-TDX, obliviatorNFK-TDX}`, vendored in-tree.
- **Layout** mirrors upstream: `obl-radix/baselines/obliviator{FK,NFK}-TDX/`.
- **Build system**: upstream `Makefile.standalone` extended with a `multiway_join` target alongside `standalone_join`.
- **Local edits** to vendored code: refactored `scalable_oblivious_join.c` into a static `oblivious_join_core` helper + two wrappers (original ASCII emit + new `_to_array`); added `sort_time_out`; widened `DATA_LENGTH`; bumped `string_key[10]` → `[16]` (latent overflow).

### Bugs already fixed
- **`char string_key[10]`** in both kernels couldn't hold an 11-char int32; bumped to `[16]`.
- **NFK double-free** of `aggregation_tree` buffers: removed our trailing `scalable_oblivious_join_free()` call (matches upstream `standalone_main.c`).

### Implementation hygiene
- All Phase 1+2 work isolated on branch `obliviator-multiway` (commit `53bda3d`); main untouched.
- Build artefacts (`*.o`, `*.a`, `*.so`, `*.d`, `multiway_join`, `standalone_join`) gitignored.

## Current stage

- **Phase 1** (widen `elem_t`) — done in both FK and NFK trees.
- **Phase 2** (driver + converter + kernel `_to_array`) — done.
- **Phase 3, task 1** (converter at K=3,4,5) — done; plans for all 5 chain depths look right.
- **Phase 3, task 2** (run all 5 depths against `banking_1k`) — done.
- **Phase 3, task 3** (`tests/test_obliviator_multiway_correctness.py` — SQLite-diff harness) — done.
- **Phase 3, task 4** (promote to `banking_200k`) — not started, blocked by the issue below.

At `--threads 1`, K=1..5 on `banking_1k` (200 accounts / 1000 txns) produce **exactly** the SQLite reference result for all five depths:

| K | Driver rows | SQLite rows | Match | Online time |
|---|---|---|---|---|
| 1 | 1000 | 1000 | ✓ | 6 ms |
| 2 | 4823 | 4823 | ✓ | 49 ms |
| 3 | 23091 | 23091 | ✓ | 260 ms |
| 4 | 110167 | 110167 | ✓ | 1584 ms |
| 5 | 526490 | 526490 | ✓ | 9609 ms |

## The blocking problem

At `--threads ≥ 2`, the NFK kernel produces **wrong cross-product pairings** in any step where both sides have duplicate keys for the same key value (`m0 > 1` *and* `m1 > 1`).

- K=1 is unaffected (account FK on both ends; m0=1 throughout).
- K≥2 fails at step 2 (intermediate `⋈` t2 on `a2.account_id = t2.acc_from`), where multiple intermediate rows can share an `a2.account_id` and that account can have multiple outgoing txns.
- The total row count is still **correct** — the bug is that some valid (a, t, a, t, a, …) tuples get **duplicated** while others are **dropped** in equal measure.
- Concretely on `banking_1k` K=2: 4821 rows match between driver and SQLite, but 2 specific tuples appear twice in the driver output and 2 different valid tuples are missing entirely.
- The fault is **deterministic** across runs at threads=2, 4, 6 — not a race. An algorithmic bug in the upstream NFK parallel cross-product path (`aggregation_tree_i`, `oblivious_distribute_elem`, `aggregation_tree_dup`, `aggregation_tree_j_order`, post-distribute bitonic sort).
- My driver and the kernel core sequencing are correct (proven by the `--threads 1` runs).

## Options for moving forward

1. **Ship with `--threads 1`.** Correct, but slower; ~50% slowdown at K=5 vs threads=2 on banking_1k. Document as a known limitation.
2. **Debug the upstream NFK parallel cross-product path.** Determinism + a small reproducer make this tractable but not 5-minute work; estimate hours-to-days.
3. **File upstream issue with the `banking_1k` K≥2 reproducer** and proceed with option 1 in parallel.
4. **Use FK at K=1** (single-thread or multi-thread, both fine) **and NFK at K≥2 with threads=1**.

Decision pending. In the meantime, the threads=1 NFK numbers are defensible for the paper: NebulaDB's narrative ("competitive with Obliviator under stronger obliviousness") doesn't hinge on Obliviator's parallelism.
