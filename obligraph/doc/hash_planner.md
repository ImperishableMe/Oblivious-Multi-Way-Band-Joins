# Hash Planner: Strategy Selection & Caching

## Overview

The hash planner (`oblivious_hashmap/include/hash_planner.hpp`) selects the fastest oblivious hash strategy for a given workload. It benchmarks four strategies at runtime and caches results to disk so subsequent runs with the same parameters skip benchmarking.

## Available Strategies

| Strategy | Build Cost | Probe Cost | Eligibility |
|----------|-----------|------------|-------------|
| **Linear Scan** | O(n) | O(n) per probe | `n < 65536` only |
| **Cuckoo Hash** | Expensive (matching + 2 sorts) | O(1) (few CMOVs) | `n > 1024 && op_num > n` |
| **Hash Bucket** | Moderate | O(bucket_size) per probe | Always eligible |
| **Two-Tier Hash** | Moderate-High (recursive) | O(sub-bin) per probe | Always eligible |

## Parameters

- **`n`**: Number of entries in the hash table (rounded to power-of-2).
- **`op_num`**: Number of probe (lookup) operations that will be performed on the table.
- **`delta_inv_log2`**: Security parameter (default 64). Controls failure probability.

The planner minimizes total cost = `build(n) + op_num × probe_cost`.

## Benchmarking Flow

On a cache miss for `(n, op_num, delta_inv_log2)`:

1. **Cuckoo**: Build + `op_num` random probes + extract. Skipped if `n <= 1024` or `op_num <= n`.
2. **Bucket**: `compute_appropriate_bucket_num()` tries different `(bucket_num, bucket_size)` combinations and returns the fastest.
3. **Linear scan**: Build + `op_num` probes + extract. Skipped if `n >= 65536`.
4. **Two-tier**: `compute_epsilon_inv()` tries `epsilon_inv = 2, 4, 8, 16, ...` (doubling), stopping when `bin_size = epsilon_inv² × 1024 >= n`. Each trial constructs a full `OTwoTierHash`, which **recursively** calls `determine_hash` for its sub-tables (major bins + overflow bin).

The strategy with the minimum wall-clock time wins.

## Two-Tier Hash Internals

The two-tier hash splits `n` elements into two levels:

- **Major bins**: `bin_num = 2n / bin_size` bins, each an `ObliviousBin(bin_size/2, n/bin_num)`.
- **Overflow bin**: One `ObliviousBin(n/epsilon_inv, n)` holding the fraction `ε = 1/epsilon_inv` of elements that spill over.

`epsilon_inv` controls the tradeoff:
- Small `epsilon_inv` → large overflow bin, many small major bins.
- Large `epsilon_inv` → small overflow bin, few large major bins.

On probe: check overflow bin first, then the appropriate major bin. Every probe touches both levels, so overflow bin size affects all lookups.

Because each sub-bin is itself an `ObliviousBin`, construction calls `determine_hash` recursively, which can trigger further benchmarks or cache hits for sub-table sizes.

## Caching

Results are stored in two files next to the executable:

- **`hash_map.bin{BlockSize}`**: Strategy cache. One line per `(n, op_num, delta)` triple with the selected strategy and its parameters. Read on first call (static map), appended on cache miss.
- **`hash_time.bin{BlockSize}`**: Timing log. Records benchmark results for all strategies tried. Used for debugging, not read back.

Cache lookup is **exact match** on `(n, op_num, delta_inv_log2)`. A different `op_num` for the same `n` triggers a new benchmark.

## The `op_num` Fix

### Problem

`buildNodeIndex()` in `oneHop.cpp` constructed `ObliviousBin(n)` without specifying `op_num`, which defaulted to 1. The planner then optimized for `build + 1 probe`, selecting strategies with cheap builds (e.g., linear scan) regardless of how many probes would actually happen.

In banking_onehop, the index is probed once per edge (e.g., 5M times for 5M transactions), so the planner was solving the wrong optimization problem.

### Impact

For the small banking dataset (16K accounts, 50K edges):

| | Strategy | Build | Probe (total) |
|--|----------|-------|---------------|
| **Before** (op_num=1) | linear | 2ms | 1,124ms |
| **After** (op_num=50001) | bucket | 2,839ms | ~20ms/side |

Linear scan was chosen because it has the cheapest build, but each of the 50K probes did a full O(n) scan. With the correct `op_num`, bucket hash was selected — more expensive build, but O(bucket_size) probes amortize over 50K lookups.

### Change

Added `op_num` parameter to `buildNodeIndex`:

- **`node_index.h`**: `buildNodeIndex(const Table& table, size_t op_num = 1)`
- **`oneHop.cpp`**: Passes `op_num` through to `ObliviousBin(n, op_num)`
- **`oneHop.cpp` (`build_and_probe`)**: Passes `probeT.rowCount` as `op_num`
- **`banking_onehop.cpp`**: Passes `catalog.getTable("txn_fwd").rowCount` as `op_num`
