// Standalone unit test: isolate obligraph::o_sort multi-thread correctness
// on entry_t arrays. Build random arrays, sort with o_sort at various T,
// verify against std::sort. Runs without the full sgx_app pipeline.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "obl_building_blocks.h"  // brings obligraph::o_sort + ThreadPool
#include "enclave_types.h"        // entry_t

extern "C" {
#include "comparator_convention.h"
}

extern "C" int compare_join_attr(entry_t* e1, entry_t* e2);

static bool entries_equal(const entry_t& a, const entry_t& b) {
    return std::memcmp(&a, &b, sizeof(entry_t)) == 0;
}

static void fill_random(std::vector<entry_t>& arr, uint32_t seed) {
    std::mt19937 rng(seed);
    // Mirror banking_10k inversion-case histogram: small join_attr range so ties
    // happen often, field_type in {SOURCE=1, START=2, END=3}, equality_type
    // ~7% EQ / ~93% NEQ. This is the distribution that breaks o_sort at T>=2
    // in the integration.
    std::uniform_int_distribution<int32_t> jd(0, 10);
    std::discrete_distribution<int32_t> ft_dist({1, 9, 9});  // SOURCE:START:END = 1:9:9
    std::bernoulli_distribution eq_dist(0.07);               // 7% EQ
    for (size_t i = 0; i < arr.size(); ++i) {
        auto& e = arr[i];
        std::memset(&e, 0, sizeof(e));
        e.join_attr = jd(rng);
        e.field_type = ft_dist(rng) + 1;       // 1, 2, or 3
        e.equality_type = eq_dist(rng) ? 0 : 1; // 0=EQ, 1=NEQ
        e.original_index = static_cast<int32_t>(i);
    }
}

static int run_one(size_t N, size_t T, uint32_t seed) {
    std::vector<entry_t> reference(N);
    fill_random(reference, seed);

    // std::sort baseline
    std::vector<entry_t> stdref = reference;
    std::sort(stdref.begin(), stdref.end(),
              [](const entry_t& a, const entry_t& b) {
                  return compare_join_attr(const_cast<entry_t*>(&a),
                                           const_cast<entry_t*>(&b)) != 0;
              });

    // o_sort at thread count T
    std::vector<entry_t> osorted = reference;
    obligraph::ThreadPool pool(std::max<size_t>(1, T));
    obligraph::o_sort<entry_t>(
        osorted.data(), 0, osorted.size(),
        [](const entry_t& a, const entry_t& b) -> bool {
            return compare_join_attr(const_cast<entry_t*>(&a),
                                     const_cast<entry_t*>(&b)) != 0;
        },
        pool, T);

    // Compare element-by-element. Allow equality-class permutations to match.
    // For this synthetic test with field_type=SOURCE, equality_type=EQ, ties only
    // happen on join_attr collisions; we compare on join_attr alone for verification.
    bool ok = true;
    for (size_t i = 0; i < N; i++) {
        if (osorted[i].join_attr != stdref[i].join_attr) {
            ok = false;
            if (i < 20) {
                printf("  [N=%zu T=%zu] mismatch at i=%zu: o_sort.join_attr=%d std.join_attr=%d\n",
                       N, T, i, osorted[i].join_attr, stdref[i].join_attr);
            }
        }
    }

    // Also check sortedness directly: is osorted non-decreasing?
    bool sorted = true;
    for (size_t i = 1; i < N; i++) {
        if (osorted[i].join_attr < osorted[i-1].join_attr) {
            sorted = false;
            if (i < 20) {
                printf("  [N=%zu T=%zu] not sorted at i=%zu: %d > %d\n",
                       N, T, i, osorted[i-1].join_attr, osorted[i].join_attr);
            }
            break;
        }
    }

    // Multiset preservation: every (join_attr, field_type, equality_type,
    // original_index) tuple in the input should appear in the output the
    // same number of times. With original_index unique, this is a permutation
    // check.
    std::vector<int32_t> in_orig, out_orig;
    in_orig.reserve(N); out_orig.reserve(N);
    for (auto& e : reference)  in_orig.push_back(e.original_index);
    for (auto& e : osorted)    out_orig.push_back(e.original_index);
    std::sort(in_orig.begin(),  in_orig.end());
    std::sort(out_orig.begin(), out_orig.end());
    bool permutation = (in_orig == out_orig);

    printf("N=%zu T=%zu seed=%u sortedAsc=%s multisetPreserved=%s sortMatchesStd=%s\n",
           N, T, seed, sorted ? "YES" : "NO",
           permutation ? "YES" : "NO", ok ? "YES" : "NO");
    return (sorted && permutation) ? 0 : 1;
}

// Like run_one but uses a shared, externally-supplied pool.
static int run_one_with_pool(size_t N, size_t T, uint32_t seed,
                             obligraph::ThreadPool& pool) {
    std::vector<entry_t> reference(N);
    fill_random(reference, seed);

    std::vector<entry_t> osorted = reference;
    comparator_func_t cmp = compare_join_attr;
    obligraph::o_sort<entry_t>(
        osorted.data(), 0, osorted.size(),
        [cmp](const entry_t& a, const entry_t& b) -> bool {
            return cmp(const_cast<entry_t*>(&a), const_cast<entry_t*>(&b)) != 0;
        },
        pool, T);

    bool sorted = true;
    for (size_t i = 1; i < N; i++) {
        if (osorted[i].join_attr < osorted[i-1].join_attr) { sorted = false; break; }
    }
    std::vector<int32_t> in_orig, out_orig;
    in_orig.reserve(N); out_orig.reserve(N);
    for (auto& e : reference)  in_orig.push_back(e.original_index);
    for (auto& e : osorted)    out_orig.push_back(e.original_index);
    std::sort(in_orig.begin(),  in_orig.end());
    std::sort(out_orig.begin(), out_orig.end());
    bool permutation = (in_orig == out_orig);
    printf("  [shared-pool T=%zu] N=%zu seed=%u sortedAsc=%s multisetPreserved=%s\n",
           T, N, seed, sorted ? "YES" : "NO", permutation ? "YES" : "NO");
    return (sorted && permutation) ? 0 : 1;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int failures = 0;

    printf("=== Independent pools ===\n");
    for (size_t N : {8u, 64u, 1024u, 20000u, 70000u, 110000u}) {
        for (size_t T : {1u, 2u, 4u, 8u, 16u}) {
            failures += run_one(N, T, /*seed=*/42);
        }
    }

    // Now mimic the integration: a single pool of size T, reused across many
    // o_sort calls with varying sizes and seeds.
    printf("\n=== Shared pool, varying N and seed (mirrors integration usage) ===\n");
    for (size_t T : {2u, 4u, 8u, 16u}) {
        obligraph::ThreadPool pool(T);
        // Mirror banking_10k JOIN_ATTR call pattern: ~12 calls each at varied sizes.
        std::mt19937 sizeRng(99);
        std::uniform_int_distribution<size_t> sizes(2000, 25000);
        for (int rep = 0; rep < 12; rep++) {
            size_t N = sizes(sizeRng);
            failures += run_one_with_pool(N, T, /*seed=*/100 + rep, pool);
        }
    }

    printf("\nTotal failures: %d\n", failures);
    return failures ? 1 : 0;
}
