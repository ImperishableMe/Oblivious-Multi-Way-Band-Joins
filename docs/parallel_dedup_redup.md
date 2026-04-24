# Parallel Deduplication / Reduplication for oneHop

> **Status:** Design spec, not yet implemented.
> **Target:** `obligraph/src/oneHop.cpp` — `deduplicateRows` and `reduplicateRows`.
> **Inspiration:** `OnOff-FK` directory of [dsg-uwaterloo/obl-radix](https://github.com/dsg-uwaterloo/obl-radix/tree/main/OnOff-FK).

## 1. Why

`deduplicateRows` and `reduplicateRows` in the oneHop pipeline are currently **single-threaded left-to-right scans**. They run four times per query (src dedup, src redup, dst dedup, dst redup) over ~edge-count rows. On the 200k banking dataset that is ~1M × 64B = 64 MB per scan, ~4×64 MB serial memory traffic per query.

The rest of oneHop (sort, probe, union, compact) is already parallelized via `ThreadPool`, so these two functions are the remaining serial bottleneck that grows linearly with table size.

`obl-radix`'s `OnOff-FK` code parallelizes structurally identical scans using a **3-phase parallel-scan pattern** that preserves obliviousness. This doc specifies how to port that pattern to our dedup/redup.

## 2. Current code (baseline)

File: [obligraph/src/oneHop.cpp:100-127](../obligraph/src/oneHop.cpp).

```cpp
void deduplicateRows(Table& table) {
    key_t lastKey = static_cast<key_t>(-1);
    key_t dummy   = 1e9;

    for (size_t i = 0; i < table.rows.size(); ++i) {
        key_t currentKey = table.rows[i].key.first;
        dummy = (static_cast<key_t>(random()) + (key_t(1) << 31)) & ~DUMMY_KEY_MSB;
        table.rows[i].key.first = ObliviousChoose(lastKey == currentKey, dummy, currentKey);
        table.rows[i].setDummy(lastKey == currentKey);
        lastKey = currentKey;
    }
}

void reduplicateRows(Table& table) {
    Row lastRow;
    for (size_t i = 0; i < table.rows.size(); ++i) {
        auto secKey = table.rows[i].key.second;
        table.rows[i] = ObliviousChoose(table.rows[i].isDummy(), lastRow, table.rows[i]);
        table.rows[i].key.second = secKey;
        lastRow = table.rows[i];
    }
}
```

### Data layout (from `obligraph/include/definitions.h`)

```cpp
struct Row {
    PairKey key;                   // pair<uint64_t, uint64_t>: (srcId, dstId)
    char data[ROW_DATA_MAX_SIZE];  // 48 bytes of column payload

    bool isDummy() const       { return (key.first & DUMMY_KEY_MSB) != 0; }
    void setDummy(bool d);
    key_t cleanKey() const     { return key.first & ~DUMMY_KEY_MSB; }
};
static_assert(sizeof(Row) == 64, "Row must be exactly 1 cache line");
```

Both functions consume a table **already sorted by `key.first`** (the join key). Dedup collapses each run of equal keys, keeping only the first as real. Redup is the inverse: it expands dummies back into copies of the previous real row.

### Threading primitive

The rest of oneHop uses `ThreadPool` + `pool.submit(lambda) → std::future<void>`. Slice sizing convention: `pool.size()` workers. Match this style; do **not** introduce `std::thread` directly.

### Obliviousness constraints

- Cannot branch on `isDummy()`, `lastKey == currentKey`, or any data value.
- Use `ObliviousChoose(cond, a, b)` for conditional selection.
- Access pattern within each thread's slice must be data-independent (it already is — it's a pure linear scan).
- Parallelization **preserves** obliviousness because each thread deterministically scans its own contiguous slice; no shared writes, no data-dependent work-stealing.

## 3. The 3-phase parallel-scan pattern

Both functions are **associative carry-forward scans** — the only cross-row dependency is a single piece of state (`lastKey` or `lastRow`) passed left-to-right. Any such scan can be restructured as:

1. **Phase 1 — local scan (P parallel workers)**: Each worker processes its contiguous slice independently, emitting a small *summary* of what crosses its right boundary (last key, last real row, slice sum, …).
2. **Phase 2 — serial merge (one thread, O(P))**: Walk the P summaries left-to-right to compute a *seed* value for each slice (the correct state that **should** have been entering that slice if the scan had been run serially).
3. **Phase 3 — fixup (P parallel workers)**: Each worker re-runs its slice, this time starting from the correct `seed[t]` instead of a blank state.

Because `P ≤ numThreads ≪ N`, Phase 2 is essentially free. Phases 1 and 3 are embarrassingly parallel.

### Worked example — dedup

Input sorted table, P=4 threads, 3 rows per slice:

```
idx :  0  1  2 | 3  4  5 | 6  7  8 | 9 10 11
key :  1  1  2 | 2  2  3 | 3  5  5 | 5  5  7
```

**Pre-pass (cheaper than Phase 1 here — only needs the single boundary key):**

```
seed[0] = SENTINEL (-1)
seed[1] = key[2]  = 2
seed[2] = key[5]  = 3
seed[3] = key[8]  = 5
```

Each worker then runs the original loop with its seed:

```
T0 seed=-1: [1 real, 1 dummy, 2 real]          (key 1 at idx 1 is dup of idx 0; key 2 at idx 2 is new)
T1 seed=2 : [2 dummy, 2 dummy, 3 real]         (idx 3 is dup of seed; idx 5 is new)
T2 seed=3 : [3 dummy, 5 real,  5 dummy]        (idx 6 is dup of seed; idx 7 is new; idx 8 is dup)
T3 seed=5 : [5 dummy, 5 dummy, 7 real]
```

Output matches the serial result exactly.

### Worked example — redup (classic 3-phase)

After dedup+probe, input looks like (`.` = dummy):

```
pos :  0  1  2  3  4  5  6  7  8  9 10 11
val :  A  .  B  .  .  C  .  D  .  .  .  E
```

Goal: `A A B B B C C D D D D E`.

**Phase 1** — each thread scans its slice, records `tail[t]` = last real row seen:

```
T0 [A,.,B] → tail[0] = B, hasReal=1
T1 [.,.,C] → tail[1] = C, hasReal=1
T2 [.,D,.] → tail[2] = D, hasReal=1
T3 [.,.,E] → tail[3] = E, hasReal=1
```

**Phase 2** — serial walk to compute each slice's seed (= "real in effect when slice starts"):

```
running = ∅
seed[0] = ∅;  running ← B
seed[1] = B;  running ← C
seed[2] = C;  running ← D
seed[3] = D;  running ← E
```

**Phase 3** — each thread re-scans with its seed, writing dummies back:

```
T0 seed=∅: [A, A, B]
T1 seed=B: [B, B, C]
T2 seed=C: [C, D, D]
T3 seed=D: [D, D, E]
```

Concatenated = correct serial result.

## 4. Implementation plan

### 4.1 Helper: slice builder

Add to a small shared header (e.g., `obligraph/include/slice_utils.h`):

```cpp
#pragma once
#include <cstddef>
#include <vector>

struct Slice { size_t begin; size_t end; };

inline std::vector<Slice> buildSlices(size_t n, size_t P) {
    std::vector<Slice> out;
    if (P == 0) P = 1;
    if (n == 0) return out;
    out.reserve(P);
    size_t q = n / P, r = n % P, off = 0;
    for (size_t t = 0; t < P; ++t) {
        size_t sz = q + (t < r ? 1 : 0);
        if (sz == 0) break;
        out.push_back({off, off + sz});
        off += sz;
    }
    return out;
}
```

### 4.2 Parallel dedup (1 pre-pass + 1 parallel scan)

Dedup is special — the boundary info is just **one key per slice**, so Phase 1 collapses to a trivial pre-pass:

```cpp
void deduplicateRowsParallel(Table& table, ThreadPool& pool) {
    const size_t N = table.rows.size();
    if (N == 0) return;
    const size_t P = std::min<size_t>(pool.size(), N);
    const auto slices = buildSlices(N, P);

    // Pre-pass (O(P), serial): capture pre-dedup last key of each slice.
    // This runs BEFORE any writes, so slice boundaries read the correct values.
    std::vector<key_t> seed(slices.size());
    seed[0] = static_cast<key_t>(-1);  // sentinel: idx 0 is always real
    for (size_t t = 1; t < slices.size(); ++t) {
        seed[t] = table.rows[slices[t-1].end - 1].key.first;
    }

    // Phase 3 (parallel): existing loop, per slice, with correct starting lastKey.
    std::vector<std::future<void>> fs;
    fs.reserve(slices.size());
    for (size_t t = 0; t < slices.size(); ++t) {
        fs.push_back(pool.submit([&, t] {
            thread_local std::mt19937_64 rng{std::random_device{}()};  // see §5
            key_t lastKey = seed[t];
            for (size_t i = slices[t].begin; i < slices[t].end; ++i) {
                key_t cur = table.rows[i].key.first;
                key_t dummy = (static_cast<key_t>(rng()) + (key_t(1) << 31))
                              & ~DUMMY_KEY_MSB;
                table.rows[i].key.first =
                    ObliviousChoose(lastKey == cur, dummy, cur);
                table.rows[i].setDummy(lastKey == cur);
                lastKey = cur;
            }
        }));
    }
    for (auto& f : fs) f.get();
}
```

**Why this is correct:**
- `seed[t]` is read from position `slices[t-1].end - 1` **before any writes begin**. By the time Phase 3 starts writing, those values have been captured by value into `seed`.
- Within slice `t`, the first row compares against `seed[t]` just as it would have compared against `key[slices[t].begin - 1]` in the serial version.
- Each slice writes only to its own range; no data races.

### 4.3 Parallel redup (full 3-phase)

Redup needs the full pattern because "last real row in slice t-1" requires scanning slice t-1's contents.

```cpp
void reduplicateRowsParallel(Table& table, ThreadPool& pool) {
    const size_t N = table.rows.size();
    if (N == 0) return;
    const size_t P = std::min<size_t>(pool.size(), N);
    const auto slices = buildSlices(N, P);

    // Phase 1 (parallel): each thread finds its "tail real row".
    std::vector<Row>     tail(slices.size());
    std::vector<uint8_t> tailReal(slices.size(), 0);

    std::vector<std::future<void>> fs;
    fs.reserve(slices.size());
    for (size_t t = 0; t < slices.size(); ++t) {
        fs.push_back(pool.submit([&, t] {
            Row cur;
            uint8_t have = 0;
            for (size_t i = slices[t].begin; i < slices[t].end; ++i) {
                bool real = !table.rows[i].isDummy();
                cur  = ObliviousChoose(real, table.rows[i], cur);
                have = ObliviousChoose(real, uint8_t{1}, have);
            }
            tail[t]     = cur;
            tailReal[t] = have;
        }));
    }
    for (auto& f : fs) f.get();
    fs.clear();

    // Phase 2 (serial, O(P)): seed[t] = real row in effect entering slice t.
    std::vector<Row> seed(slices.size());
    Row running;                  // default-constructed "no real yet"
    uint8_t runHave = 0;
    for (size_t t = 0; t < slices.size(); ++t) {
        seed[t] = running;
        running = ObliviousChoose(tailReal[t] != 0, tail[t], running);
        runHave = ObliviousChoose(tailReal[t] != 0, uint8_t{1}, runHave);
    }

    // Phase 3 (parallel): original redup loop per slice, seeded.
    for (size_t t = 0; t < slices.size(); ++t) {
        fs.push_back(pool.submit([&, t] {
            Row lastRow = seed[t];
            for (size_t i = slices[t].begin; i < slices[t].end; ++i) {
                auto secKey = table.rows[i].key.second;
                table.rows[i] = ObliviousChoose(
                    table.rows[i].isDummy(), lastRow, table.rows[i]);
                table.rows[i].key.second = secKey;
                lastRow = table.rows[i];
            }
        }));
    }
    for (auto& f : fs) f.get();
}
```

### 4.4 Call-site swap

In `buildSourceAndEdgeTables` / `buildDestinationTable` ([obligraph/src/oneHop.cpp:150, 160, 201, 211, 257, 267](../obligraph/src/oneHop.cpp)), replace:

```cpp
deduplicateRows(srcSide);   →   deduplicateRowsParallel(srcSide, pool);
reduplicateRows(srcSide);   →   reduplicateRowsParallel(srcSide, pool);
```

Keep the serial versions for now behind the new name as a reference / fallback.

## 5. Subtleties & gotchas

1. **`random()` is not thread-safe.** The serial version calls `random()` from `<stdlib.h>`, which uses a global state and is documented unsafe under concurrent access. Replace with `thread_local std::mt19937_64` seeded from `std::random_device` (shown in §4.2) or from a per-thread AES-PRF keyed off a process-wide secret (obl-radix's approach in `replace_dummies.h` — more oblivious but heavier).
2. **Obliviousness of `bool real = !isDummy()`.** This is a read-only branch predicate, identical to the serial code's `isDummy()` check inside `ObliviousChoose`. No new side channel.
3. **`Row` default-construction.** `Row` must default to `isDummy() == true` (or at least non-aliasing any real row) so that the leading `seed[0]` in redup doesn't inject garbage data when the table starts with a dummy. Verify in `definitions.h`.
4. **Empty slices when `P > N`.** Guarded by `P = min(pool.size(), N)` and `buildSlices` skipping zero-size slices.
5. **`Row` is 64 B (one cache line).** Slice boundaries land on row granularity → no false sharing across threads.
6. **Memory ordering.** Each slice's Phase 3 only reads values from Phase 2's `seed[]` vector (populated before `pool.submit` loops) and writes to disjoint rows. No atomics needed; the `future::get()` barrier between phases provides acquire/release.

## 6. Expected performance impact

### Direct speedup
- `deduplicateRows` and `reduplicateRows` each run 2× per query (src and dst). On a 200k banking / 1M-edge dataset, each call scans ~1M × 64 B = 64 MB.
- Serial memory-bound cost: ~20–40 ms per call at 10–15 GB/s on a modern laptop; ~80–160 ms across all four calls.
- Dedup (~1 memory pass) should scale near-linearly with P up to memory bandwidth; redup (2 passes) slightly less.
- At P = 16: expect the four calls combined to drop from ~80–160 ms to ~15–30 ms.

### Indirect (pipeline) speedup
- src/dst branches already run concurrently via `std::async`. Today the critical path includes each branch's serial dedup + redup (the other can't help). After parallelization both branches finish their dedup/redup sooner, pulling in the `ONLINE parallel branches (wall)` total.
- Expected end-to-end: **~2–5% off oneHop on the 200k baseline** (2.3 s → ~2.2–2.25 s).
- The relative win grows with scale: dedup/redup is O(N), most other oneHop stages are well-parallelized, so at 10× scale the dedup/redup share of total time grows. Worth doing before any scale-up experiments.

## 7. Correctness & regression testing

Per [CLAUDE.md](../CLAUDE.md), after any change to `obligraph/src/`:

```bash
# Rebuild banking_onehop
cmake --build obligraph/build --target banking_onehop

# Correctness regression #1 — oneHop binary output vs SQLite
python3 tests/test_onehop_correctness.py \
    input/plaintext/banking_1k obligraph/build/banking_onehop

# Correctness regression #2 — full pipeline vs SQLite
python3 tests/test_pipeline_correctness.py \
    input/plaintext/banking_1k obligraph/build/banking_onehop ./sgx_app

# Performance regression (requires input/plaintext/banking_200k)
python3 tests/test_large_scale_regression.py \
    input/plaintext/banking_200k obligraph/build/banking_onehop ./sgx_app
```

Target: ONLINE oneHop stays ≤ baseline (~2.3 s) and ideally improves.

## 8. Follow-up (out of scope for this change)

- **Fold the pre-dedup copy loop into Phase 3.** [oneHop.cpp:146-150](../obligraph/src/oneHop.cpp) does `addRow` + `rows[i].key = edge.key` sequentially right before calling dedup. That's another 1M-iteration serial loop over the same memory. Parallelize it with the same slice pattern, or fuse it with Phase 3 of parallel dedup so the data touches cache once.
- **Bigger prize — replace `parallel_sort (dst)` with the obl-radix expand pipeline.** Obl-radix's dedup stores `cntSelf = group_size` on the survivor, enabling a `prefixSumExpandParallel` + `obli_distribute_rows` + `carryForwardParallel` flow that produces the FK-side expansion **without** an oblivious sort at the end. The dst-side `parallel_sort` at [oneHop.cpp:231](../obligraph/src/oneHop.cpp) is historically one of the largest ONLINE stages. A structural refactor along those lines is a much bigger lever than this micro-parallelization, but also a much bigger change. Do this one first to learn the pattern, then consider that one.

## 9. References

- [obl-radix/OnOff-FK/parallel_counts.h](https://github.com/dsg-uwaterloo/obl-radix/blob/main/OnOff-FK/parallel_counts.h) — 4-phase parallel dedup with `cntSelf` (more complex than ours because it also computes group sizes).
- [obl-radix/OnOff-FK/carry_forward.h](https://github.com/dsg-uwaterloo/obl-radix/blob/main/OnOff-FK/carry_forward.h) — canonical 3-phase left-to-right carry-forward, the template for our redup.
- [obl-radix/OnOff-FK/backfill_dummies.h](https://github.com/dsg-uwaterloo/obl-radix/blob/main/OnOff-FK/backfill_dummies.h) — same pattern right-to-left.
- [obl-radix/OnOff-FK/prefix_sum_expand.h](https://github.com/dsg-uwaterloo/obl-radix/blob/main/OnOff-FK/prefix_sum_expand.h) — same pattern applied to prefix sums (for the "bigger prize" follow-up).
