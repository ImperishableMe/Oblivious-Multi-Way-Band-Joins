#pragma once

/**
 * OrShuffle - Oblivious Shuffle using OrCompact with Random Marking
 *
 * Implements an oblivious shuffle algorithm based on the OrShuffle algorithm
 * from the oblsort repository (https://github.com/gty929/oblsort).
 *
 * Algorithm:
 *   OrShuffle(data, n):
 *     if n <= 1: return
 *     if n == 2: random swap and return
 *
 *     1. Randomly mark ~n/2 elements for left half using prefix sums
 *     2. OrCompact to move marked elements to front (obliviously)
 *     3. Recursively OrShuffle(left_half)
 *     4. Recursively OrShuffle(right_half)
 *
 * Advantages over Waksman permutation network:
 * - Does NOT require power-of-2 sizes (simplifies padding logic)
 * - Leverages existing OrCompact SIMD primitives
 * - Same O(n log n) complexity
 * - Better cache locality potential
 */

#include "../../oblivious_hashmap/include/ocompact.hpp"
#include "../../oblivious_hashmap/include/oblivious_operations.hpp"
#include "../../common/enclave_types.h"
#include "../../common/debug_util.h"
#include <vector>
#include <random>
#include <bit>

namespace OrShuffle {

/**
 * Get the next power of 2 greater than or equal to n
 */
inline size_t next_power_of_2(size_t n) {
    if (n == 0) return 1;
    return std::bit_ceil(n);
}

/**
 * Internal recursive OrShuffle for power-of-2 sizes only
 * This is called after padding has been applied at the top level.
 */
template <typename T>
void or_shuffle_pow2_impl(T* data, size_t n, std::mt19937& rng) {
    if (n <= 1) return;

    if (n == 2) {
        // Base case: random swap with probability 0.5
        std::uniform_int_distribution<int> dist(0, 1);
        ORAM::obliSwap(data[0], data[1], dist(rng));
        return;
    }

    size_t half_size = n / 2;
    std::vector<uint8_t> marks(n, 0);

    // Random marking with exact count tracking
    // Mark exactly half_size elements for the left partition
    size_t k = half_size;
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    for (size_t i = 0; i < n; i++) {
        size_t remaining = n - i;
        double prob = static_cast<double>(k) / static_cast<double>(remaining);
        bool mark = uniform(rng) < prob;
        marks[i] = mark ? 1 : 0;
        if (mark) k--;
    }

    // n is already a power of 2
    ORAM::or_compact_power_2(data, marks.data(), n);

    // Recursive shuffle on both halves (both are power of 2)
    or_shuffle_pow2_impl(data, half_size, rng);
    or_shuffle_pow2_impl(data + half_size, half_size, rng);
}

/**
 * OrShuffle implementation that handles arbitrary sizes by padding
 * Pads to next power of 2 at the top level, shuffles, then extracts original elements
 *
 * NOTE: This version uses the fact that we're shuffling entry_t which has an
 * original_index field. We mark padding entries with original_index < 0.
 */
inline void or_shuffle_impl(entry_t* data, size_t n, std::mt19937& rng) {
    if (n <= 1) return;

    size_t padded_n = next_power_of_2(n);

    if (padded_n == n) {
        // Already a power of 2, use optimized path
        or_shuffle_pow2_impl(data, n, rng);
    } else {
        // Pad to power of 2
        std::vector<entry_t> padded_data(padded_n);
        std::copy(data, data + n, padded_data.begin());

        // Mark padding entries with negative original_index
        for (size_t i = n; i < padded_n; i++) {
            memset(&padded_data[i], 0, sizeof(entry_t));
            padded_data[i].original_index = -1;  // Mark as padding
        }

        // Shuffle the padded array
        or_shuffle_pow2_impl(padded_data.data(), padded_n, rng);

        // Create flags array based on original_index
        // original_index >= 0 means it's a real element (flag = 1)
        std::vector<uint8_t> is_original(padded_n);
        for (size_t i = 0; i < padded_n; i++) {
            is_original[i] = (padded_data[i].original_index >= 0) ? 1 : 0;
        }

        // Use OrCompact to move original elements to the front
        ORAM::or_compact_power_2(padded_data.data(), is_original.data(), padded_n);

        // First n elements are now the shuffled original elements
        std::copy(padded_data.begin(), padded_data.begin() + n, data);
    }
}

/**
 * Main entry point for OrShuffle on entry_t arrays
 *
 * Performs an oblivious shuffle of the given array in-place.
 * This is the function called by ShuffleManager.
 *
 * @param data Pointer to array of entry_t to shuffle
 * @param n Number of entries
 * @return 0 on success, -1 on error
 */
inline int or_shuffle(entry_t* data, size_t n) {
    DEBUG_INFO("=== or_shuffle START: n=%zu ===", n);

    if (!data || n == 0) {
        DEBUG_ERROR("Invalid parameters: data=%p, n=%zu", (void*)data, n);
        return -1;
    }

    if (n == 1) {
        DEBUG_INFO("=== or_shuffle END: single element, nothing to do ===");
        return 0;
    }

    // Initialize random number generator
    std::random_device rd;
    std::mt19937 rng(rd());

    DEBUG_INFO("Starting OrShuffle: n=%zu", n);
    or_shuffle_impl(data, n, rng);

    DEBUG_INFO("=== or_shuffle END: SUCCESS ===");
    return 0;
}

/**
 * OrShuffle with a specific seed (useful for testing/reproducibility)
 *
 * @param data Pointer to array of entry_t to shuffle
 * @param n Number of entries
 * @param seed Random seed for reproducibility
 * @return 0 on success, -1 on error
 */
inline int or_shuffle_seeded(entry_t* data, size_t n, uint32_t seed) {
    DEBUG_INFO("=== or_shuffle_seeded START: n=%zu, seed=%u ===", n, seed);

    if (!data || n == 0) {
        DEBUG_ERROR("Invalid parameters: data=%p, n=%zu", (void*)data, n);
        return -1;
    }

    if (n == 1) {
        DEBUG_INFO("=== or_shuffle_seeded END: single element, nothing to do ===");
        return 0;
    }

    // Initialize random number generator with provided seed
    std::mt19937 rng(seed);

    DEBUG_INFO("Starting OrShuffle (seeded): n=%zu", n);
    or_shuffle_impl(data, n, rng);

    DEBUG_INFO("=== or_shuffle_seeded END: SUCCESS ===");
    return 0;
}

} // namespace OrShuffle
