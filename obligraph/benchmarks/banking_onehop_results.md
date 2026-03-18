# Banking One-Hop Benchmark Results

**Date:** 2026-02-27
**Binary:** `banking_onehop` (cache-line-row-optimization branch)
**Machine:** AMD EPYC 7R13, 16 cores / 32 HW threads
**Row size:** 64 bytes (cache-line aligned)

## Per-Thread-Count Results

### 16 Threads (1 per physical core)

| Dataset | Max Probing Time (ms) | One-Hop Online Time (ms) |
|---------|-----------------------|--------------------------|
| 100K    | 528                   | 881                      |
| 500K    | 8,437                 | 9,307                    |
| 1M      | 20,527                | 22,115                   |

### 32 Threads (all HW threads — baseline)

| Dataset | Max Probing Time (ms) | One-Hop Online Time (ms) |
|---------|-----------------------|--------------------------|
| 100K    | 869                   | 1,119                    |
| 500K    | 7,580                 | 8,653                    |
| 1M      | 18,143                | 20,432                   |

### 64 Threads (2x over-subscription)

| Dataset | Max Probing Time (ms) | One-Hop Online Time (ms) |
|---------|-----------------------|--------------------------|
| 100K    | 872                   | 1,294                    |
| 500K    | 7,424                 | 8,420                    |
| 1M      | 16,195                | 18,214                   |

## Full Comparison (Online Time)

| Dataset | 16t (ms) | 32t (ms) | 64t (ms) | 16→32 Δ | 32→64 Δ | Best |
|---------|----------|----------|----------|---------|---------|------|
| 100K    | 881      | 1,119    | 1,294    | +27.0%  | +15.6%  | 16t  |
| 500K    | 9,307    | 8,653    | 8,420    | -7.0%   | -2.7%   | 64t  |
| 1M      | 22,115   | 20,432   | 18,214   | -7.6%   | -10.9%  | 64t  |

## Full Comparison (Max Probing Time)

| Dataset | 16t (ms) | 32t (ms) | 64t (ms) | 16→32 Δ | 32→64 Δ | Best |
|---------|----------|----------|----------|---------|---------|------|
| 100K    | 528      | 869      | 872      | +64.6%  | +0.3%   | 16t  |
| 500K    | 8,437    | 7,580    | 7,424    | -10.2%  | -2.1%   | 64t  |
| 1M      | 20,527   | 18,143   | 16,195   | -11.6%  | -10.7%  | 64t  |

## Analysis

**100K:** 16 threads is the clear winner (881 ms online). At this small scale, thread
management and synchronization overhead dominate — more threads just add cost.

**500K–1M:** More threads consistently help. 64 threads is best, with the gap widening
at larger scales (1M: 64t is 17.6% faster than 16t overall). The memory-bound oblivious
hashmap probing benefits from over-subscription because extra threads hide memory latency
while others stall on cache misses.

**Recommendation:** Use 64 threads for production workloads (≥500K). For small datasets,
16 threads would be optimal but the absolute difference is small (~400 ms).

**Metrics:**
- **Max Probing Time**: The larger of the two concurrent `Probe with Pre-built Index` timers (source side vs destination side).
- **One-Hop Online Time**: End-to-end online phase including probing, projections, unions, sorting, and filtering.

---

## Thread-Local Dummy Counters (2026-03-11)

**Branch:** `thread-local-dummy-counters` (`a59a470`)
**Threads:** 32

| Dataset | Index Build (ms) | Max Probing Time (ms) | One-Hop Online (ms) | Result Rows |
|---------|------------------|-----------------------|----------------------|-------------|
| 100K    | 330              | 223                   | 490                  | 500,001     |
| 500K    | 1,114            | 3,973                 | 5,237                | 2,500,001   |
| 1M      | 2,267            | 5,002                 | 11,220               | 5,000,001   |

### Comparison vs Previous 32-Thread Baseline

| Dataset | Probing Before (ms) | Probing After (ms) | Speedup | Online Before (ms) | Online After (ms) | Speedup |
|---------|--------------------|--------------------|---------|--------------------|--------------------|---------|
| 100K    | 869                | 223                | 3.9x    | 1,119              | 490                | 2.3x    |
| 500K    | 7,580              | 3,973              | 1.9x    | 8,653              | 5,237              | 1.7x    |
| 1M      | 18,143             | 5,002              | 3.6x    | 20,432             | 11,220             | 1.8x    |

Thread-local strided dummy counters eliminate atomic contention in the oblivious hashmap probe, yielding 1.9-3.9x probing speedup and 1.7-2.3x end-to-end online speedup at 32 threads.

---

## High-Ratio Banking Datasets (2026-03-12)

**Branch:** `thread-local-dummy-counters` (`2f6ba4c`)
**Threads:** 32
**Dataset type:** High transaction-to-account ratio (banking_high_ratio)

| Dataset | Accounts | Txn:Acct Ratio | Index Build (ms) | Max Probing Time (ms) | One-Hop Online (ms) | Result Rows |
|---------|----------|----------------|------------------|-----------------------|----------------------|-------------|
| 1M      | 1,600    | 625            | 10               | 160                   | 626                  | 1,000,001   |
| 5M      | 3,125    | 1,600          | 43,696           | 656                   | 3,001                | 5,000,001   |
| 10M     | 5,000    | 2,000          | 158,422          | 4,337                 | 9,674                | 10,000,001  |

**Notes:**
- High-ratio datasets have far fewer accounts relative to transactions (~625-2000 txn per account vs ~5 in standard banking datasets).
- The 1M dataset uses a simple `hash_bucket` strategy (small n=2048) with very fast index build (10 ms).
- The 5M dataset also uses `hash_bucket` but with larger parameters, causing a 43s index build.
- The 10M dataset switches to `oblivious two_tier` strategy (epsilon=2), with the most expensive index build (158s).
- Online probe times scale roughly linearly: 1M→5M is ~4.8x, 5M→10M is ~3.2x.
