# Obliviator Multi-Way Driver — Design Decisions

Q&A log of every decision made while building the Obliviator multi-way baseline driver in `obl-radix/baselines/obliviatorNFK-TDX/` (and the earlier FK attempt in `obl-radix/baselines/obliviatorFK-TDX/`). One-line Q&A — read top-to-bottom.

## Naming / scope

- Q: What is "Obliviator" (sometimes mis-heard as "Observator" in early discussion)? A: The oblivious-join system at `github.com/dsg-uwaterloo/obl-radix`, our paper's fastest oblivious baseline.
- Q: What is "JustJoin"? A: Shorthand for "just-a-join" — the single pairwise `scalable_oblivious_join` kernel that the multi-way driver chains together, not a separate system.
- Q: Did we have a pre-existing multi-way driver to extend? A: No — the upstream only ships single-pairwise binaries, so the multi-way driver is built from scratch here.

## Threat model (what leaks, what doesn't)

- Q: Are intermediate pairwise-join sizes allowed to leak? A: Yes — this is the one extra leakage Obliviator tolerates in exchange for using sequential pairwise joins.
- Q: Can single-table filters be pushed into the CSV loader or applied between steps? A: No — filters may only be applied to the final join output; anything earlier would leak value-dependent cardinalities beyond what the threat model permits.
- Q: Does the final-stage filter itself have to be oblivious? A: No — a plain row-drop filter is sound because the final output size already leaks under the threat model.
- Q: Can we drop unused columns between steps (projection)? A: Only position-based projection whose field indices depend on the query alone, not on row values.
- Q: Is SQLite used as a performance baseline? A: No — SQLite is a correctness-only reference that every system's output is cross-checked against.

## Query shapes and workloads

- Q: Which query shapes are we shipping first? A: k-hop chains (1-hop through 5-hop) across all four workloads; bushy shapes (star, fanin, fanout, tree) land in a later phase.
- Q: Which workloads are targeted? A: Banking W1, IBM AML-Data W4, LDBC W2, SNAP W3 — all per `docs/workloads.md`.
- Q: Will we hand-write plan files first or start from a SQL-driven planner? A: Hand-write plans per workload for the MVP; add a SQL-driven planner in Phase 3.

## Plan format and driver interface

- Q: How does the driver learn the join sequence? A: From a plain-text plan file with `num_steps`, per-step `side0=/side1=/next_key_col=` lines, plus `final_filter` and `schema` directives.
- Q: Is the plan format kernel-agnostic? A: Yes — the same plan file drives either the FK or NFK multiway binary; only the linked kernel differs.
- Q: How are base-table rows brought into memory? A: Pre-converted from CSV into Obliviator one-side text files (`<row_count>` header + `<key> <data>` lines) by a per-workload Python converter; the C driver never parses CSV.
- Q: How are intermediates passed between steps? A: In-memory as `elem_t[]` pairs returned by the kernel — no text/disk round-trip between steps.
- Q: How is the next step's join key extracted from the previous output? A: By column index in the comma-separated concat of `L.data + "," + R.data`; positions are pre-computed by the planner from the query alone.
- Q: Which side is the prior intermediate and which is the new base at each step? A: For NFK, side0 = prior intermediate (`table_0=true`), side1 = new base table (`table_0=false`); concat preserves schema order as prior-columns, then new-columns.
- Q: What columns are carried forward in each intermediate row? A: All columns of every base table joined so far, comma-separated in `.data`; no between-step projection in the MVP.
- Q: Are side files deduplicated across steps? A: Yes — one file per unique (table, key-column) pair (e.g., single `account.txt` and single `txn.txt` serve every step of a Banking chain).

## Kernel selection

- Q: Which Obliviator kernel variant do we use? A: NFK (non-foreign-key) for every step — not a per-step dispatch between FK and NFK.
- Q: Why not dispatch FK for pure-FK steps and NFK elsewhere? A: FK and NFK have incompatible `elem_t` layouts and colliding symbol names; one-kernel-everywhere is far simpler at <20% overall perf cost.
- Q: Is NFK correct on FK-semantic joins (unique keys on one side)? A: Yes — when one side's keys are unique, NFK's cross-product degenerates to the same 1:N pairing FK produces.
- Q: What's the cost of NFK-everywhere vs an optimal mixed driver? A: ~10–15% slower overall, because only 2 of 2k steps in a k-hop chain are pure-FK.
- Q: Did we discover this only after implementing FK? A: Yes — the banking 1-hop FK run returned 199 rows (vs expected 988) because FK treats the intermediate as unique-keyed; that bug pushed us to vendor NFK.
- Q: Is the FK driver deleted? A: No — it stays in `obl-radix/baselines/obliviatorFK-TDX/` as reference; the paper baseline only runs the NFK `multiway_join`.

## Per-element layout

- Q: What is `DATA_LENGTH` (the `.data[]` byte budget per `elem_t`)? A: 640 bytes, sized from the AML 5-hop peak of ~623 B, compile-time overridable via `-DDATA_LENGTH=N`.
- Q: Why a single fixed ceiling instead of per-query sizing? A: Keeps `sizeof(elem_t)` known to the oblivious sort primitives and avoids maintaining a per-workload build matrix.
- Q: Does NFK's `int key` (vs FK's `long long`) constrain scale? A: No — all workload IDs in `docs/workloads.md` fit in int32; we can widen later if a future workload breaks this.

## Timing contract

- Q: What counts as "Obliviator latency" in reported numbers? A: Kernel time (sort + aggregate + compact + distribute) + in-memory merge/concat/key-pull between steps + final filter pass.
- Q: What is explicitly excluded? A: Initial CSV read into memory, and the final CSV write.
- Q: Does any intermediate ever touch disk? A: No — all intermediates live in `elem_t[]` until the final CSV write.
- Q: What timing granularity is reported per step? A: Sort time, join total, merge-buffer time, and build-side0 time for each step, plus grand totals (`total_sort_sec`, `total_online_sec`, `final_rows`) at the end.

## Build and vendoring

- Q: Where does the Obliviator source come from? A: `github.com/dsg-uwaterloo/obl-radix/tree/main/baselines/{obliviatorFK-TDX, obliviatorNFK-TDX}`, shallow-cloned and rsync-ed in.
- Q: Where do the vendored baselines live locally? A: `obl-radix/baselines/obliviatorFK-TDX/` and `obl-radix/baselines/obliviatorNFK-TDX/`, mirroring the upstream sub-tree.
- Q: What was excluded from the vendor import? A: `.idea/` IDE config and `*.o` / `*.d` build artifacts; everything else (including `third_party/liboblivious`) is vendored.
- Q: Which files are locally modified and must be preserved across re-vendor? A: `common/elem_t.h`, `enclave/scalable_oblivious_join.{c,h}`, `standalone_main.c`, and the new `multiway_main.c` / `convert_*_multiway.py` / `Makefile.standalone` changes.
- Q: What build system drives the binaries? A: The upstream `Makefile.standalone` in each baseline dir, extended with a `multiway_join` target alongside `standalone_join`.
- Q: Are both FK and NFK binaries produced? A: Yes — both compile; the paper's Obliviator baseline exercises only the NFK `multiway_join`.
- Q: Where does the new kernel entry point live? A: Added as `scalable_oblivious_join_to_array` in the same `scalable_oblivious_join.c`, with an internal static `oblivious_join_core` helper shared with the original ASCII-emitting entry point.
- Q: Why share a helper instead of duplicating the body? A: Keeps FK and NFK originals byte-stable relative to upstream and prevents the multi-way variants from drifting away from the text variants.

## Bugs found during the port

- Q: Latent buffer-size bug in both kernels? A: `char string_key[10]` in the ASCII emitter couldn't hold an 11-char int32 (`-1073741820`) — bumped to `[16]` in both FK and NFK.
- Q: Latent FK kernel UB we did not fix? A: `o_strcmp` reads 105 chars unconditionally (safe only when `DATA_LENGTH ≥ 105`) — flagged but not ours to patch since `o_strcmp` isn't exercised by our driver.
- Q: Why did the driver originally double-free the NFK aggregation-tree buffers? A: NFK's `scalable_oblivious_join_free` redundantly calls `aggregation_tree_free()` which the kernel core already calls per step; we dropped the trailing `_free()` from the driver to match upstream's `standalone_main.c`.

## Correctness verification (MVP)

- Q: How was correctness sanity-checked before moving on? A: Banking 1-hop (200 accounts / 1000 txns, filter `balance > 10000`) returned 988 rows, and 2-hop (no filter) returned 4823 rows — both exactly match a brute-force Python reference.
- Q: Is a full correctness harness wired up yet? A: No — integration with `tests/test_large_scale_regression.py` (SQLite cross-check across all workloads) is Phase 3 work.

## Open / deferred

- Q: Bushy shapes (star, fanin, fanout, tree)? A: Deferred to a second cut once chains are solid; needs a hub-based planner that picks a central alias and absorbs spokes.
- Q: SQL-driven planner? A: Deferred; until then, per-workload Python converters generate plans directly.
- Q: FK/NFK per-step dispatch? A: Deferred; revisit only if NFK-everywhere proves materially too slow for the paper's numbers.
- Q: Final-stage filter as oblivious compaction instead of plain drop? A: Deferred; the plain filter is sound under the current threat model.
- Q: Widening `int key` to `long long` in NFK? A: Deferred; workloads fit in int32.
