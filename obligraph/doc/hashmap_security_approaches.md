# Fixing Hashmap Security Leakage in One-Hop Algorithm

## 1. Problem Statement

### The Security Leakage

Our current hashmap implementation in the one-hop algorithm reveals information about the relationship between build and probe tables. When probing, keys that match entries in the build table access the same buckets as those entries. An adversary observing memory access patterns can count how many unique keys from the probe table have matches in the build table.

### Why This Matters

- **Violates oblivious execution model**: Memory access patterns should be independent of data values
- **Leaks selectivity/join cardinality information**: Adversary learns about the data distribution
- **Undermines ForwardFill's degree-hiding guarantees**: Our existing optimization assumes hash access is hidden

### Current Implementation

In `oneHop.cpp` (lines 40-97), the build-probe phase works as follows:
- **Build phase**: Iterates through build table, hashes each key, chains entries in buckets
- **Probe phase**: For each probe key, hashes and walks the bucket chain
- **Issue**: Equal keys deterministically map to the same bucket, revealing match patterns

---

## 2. Approach 1: Oblivious Hashmap

### Core Idea

Replace the current hashmap with a fully oblivious data structure where every access looks identical regardless of the key.

### How It Works

- Every lookup touches all buckets (or uses ORAM to touch O(log n) random-looking locations)
- Build phase: Insert each element by touching all buckets obliviously
- Probe phase: Each lookup scans all buckets, using `ObliviousChoose` to select the match
- Still need edge table deduplication before access (already have this)

### Pros

| Advantage | Description |
|-----------|-------------|
| **Perfect security** | Information-theoretic guarantee, no leakage whatsoever |
| **Conceptually simple** | The hashmap itself is the fix, algorithm structure unchanged |
| **Preserves ForwardFill** | Can layer on top of existing optimizations |

### Cons

| Disadvantage | Description |
|--------------|-------------|
| **Performance cost** | O(n) per lookup for linear scan approach |
| **ORAM alternative** | O(log n) per lookup but significant implementation complexity |
| **Total cost** | For n build entries and m probes, O(n × m) worst case |
| **Implementation effort** | High - need new oblivious hashmap implementation |

### When to Choose

Choose this approach if:
- Probabilistic guarantees are unacceptable
- Your threat model requires worst-case security
- Performance overhead is acceptable for your use case

---

## 3. Approach 2: Probabilistic Padding + Oblivious Shuffle

### Core Idea

Instead of making the hashmap oblivious, make the input data distribution uniform so all buckets contain roughly equal elements.

### How It Works

1. **Calculate target bucket size**: Based on table size and bucket count, determine how many elements each bucket should have
2. **Pad the build table**: Add dummy elements with random keys until every bucket has the target number of elements
3. **Oblivious shuffle**: Randomly permute the padded array (hides original positions)
4. **Build regular hashmap**: Now bucket sizes are uniform by construction
5. **Same for probe table**: Pad and shuffle before probing
6. **Filter dummies from output**: Already have this via deduplication/reduplication

### Pros

| Advantage | Description |
|-----------|-------------|
| **Good performance** | After shuffle, hashmap operations are O(1) |
| **Uses existing primitives** | We already have oblivious sort/shuffle in `obl_building_blocks.h` |
| **Lower implementation risk** | Wraps existing hashmap rather than replacing it |
| **Probabilistic guarantee** | With proper parameters, failure probability is negligible (e.g., 2^-128) |

### Cons

| Disadvantage | Description |
|--------------|-------------|
| **Statistical security** | Not information-theoretic, relies on randomness |
| **Memory overhead** | Padding increases table size |
| **Shuffle cost** | O(n log n) for oblivious shuffle (one-time cost) |
| **Parameter tuning** | Need to calculate correct padding based on balls-into-bins analysis |

### When to Choose

Choose this approach if:
- Practical performance is a priority
- Statistical/probabilistic security is acceptable
- You want to leverage existing Snoopy primitives

---

## 4. Comparison Summary

| Aspect | Oblivious Hashmap | Padding + Shuffle |
|--------|-------------------|-------------------|
| **Security Model** | Information-theoretic | Statistical |
| **Leakage** | Zero | Negligible (probabilistic) |
| **Probe Cost** | O(n) or O(log n) per lookup | O(1) per lookup |
| **Setup Cost** | O(n) build | O(n log n) shuffle |
| **Memory Overhead** | Same as original | Padding overhead |
| **Implementation Effort** | High (new data structure) | Medium (use existing primitives) |
| **Integration Risk** | High | Lower |
| **Code Reuse** | Low | High (Snoopy primitives) |

---

## 5. Recommendation

### Preferred Approach: Padding + Shuffle (Approach 2)

For our project, Approach 2 is likely more practical:

1. **Existing primitives**: We already have Snoopy-based oblivious operations in `obl_primitives.h`
2. **Amortized cost**: The shuffle cost is paid once and amortized across all probes
3. **Probe performance**: Probe-heavy workloads benefit from O(1) per-lookup
4. **Standard practice**: Probabilistic security with negligible failure is standard in this research area

### When Approach 1 May Be Better

- Our threat model strictly requires information-theoretic guarantees
- We're willing to accept O(n) overhead per probe
- Future work needs a reusable oblivious hashmap component

---

## 6. Open Questions for Discussion

1. **Acceptable failure probability**: Is 2^-40 sufficient, or do we need 2^-128?

2. **Padding strategy**: How much padding is needed? Depends on:
   - Number of buckets (B)
   - Table size (n)
   - Desired maximum bucket overflow

3. **Integration with ForwardFill**: Does padding interact with duplicate suppression?
   - Deduplication happens before padding
   - Padding happens before shuffle
   - Need to verify this ordering is correct

4. **Benchmarking**: Should we prototype both to measure actual overhead?
   - Implement simple versions of both
   - Measure on representative workloads
   - Report performance/security tradeoff quantitatively

---

## Key Files in Codebase

| File | Relevance |
|------|-----------|
| `obligraph/src/oneHop.cpp:40-97` | Current build-probe implementation |
| `obligraph/include/obl_primitives.h` | Snoopy oblivious operations |
| `obligraph/include/obl_building_blocks.h` | Parallel oblivious sort/shuffle |
| `technical_approach.md` | Threat model and security goals |
| `report.md` | ForwardFill algorithm description |

---

## Next Steps

1. Discuss this document with the research group
2. Decide on acceptable security/performance tradeoff
3. If Approach 2: Calculate specific padding parameters
4. Prototype chosen approach
5. Benchmark against current implementation
