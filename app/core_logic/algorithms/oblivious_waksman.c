/**
 * Oblivious 2-way Waksman Shuffle Implementation
 *
 * Implements a data-oblivious shuffle using Waksman permutation network.
 * All memory accesses are independent of data values to prevent side-channel attacks.
 */

#include "oblivious_waksman.h"
#include "../../../common/debug_util.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

// RNG state for deterministic switch generation
typedef struct {
    uint64_t shuffle_seed;  // Seed for this shuffle
} ShuffleRNG;

/**
 * Initialize RNG for this shuffle operation
 * Uses current time and random bits for seed
 */
static void init_shuffle_rng(ShuffleRNG* rng) {
    // Get random seed from stdlib
    rng->shuffle_seed = (uint64_t)time(NULL) ^ ((uint64_t)rand() << 32) ^ (uint64_t)rand();
}

/**
 * Get switch bit using hash function
 * Returns 0 for straight, 1 for cross
 */
static uint8_t get_switch_bit(ShuffleRNG* rng, uint32_t level, uint32_t position) {
    // Use hash function to generate random bit deterministically
    uint64_t hash = rng->shuffle_seed ^ ((uint64_t)level << 32) ^ position;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash & 1;  // Return single bit
}

/**
 * Oblivious swap using constant-time operations
 * Swaps if swap=1, doesn't swap if swap=0
 * No branches based on swap value
 */
static void oblivious_swap(entry_t* a, entry_t* b, uint8_t swap) {
    // Create mask: 0x00 if swap=0, 0xFF if swap=1
    uint8_t mask = (uint8_t)(-(int8_t)swap);
    
    // XOR swap without branches
    uint8_t* pa = (uint8_t*)a;
    uint8_t* pb = (uint8_t*)b;
    
    for (size_t i = 0; i < sizeof(entry_t); i++) {
        uint8_t diff = pa[i] ^ pb[i];
        pa[i] ^= diff & mask;
        pb[i] ^= diff & mask;
    }
}

/**
 * Recursive 2-way Waksman network
 * 
 * @param array Array of entries to shuffle
 * @param start Starting index in the array for my group
 * @param stride Distance between consecutive elements of my group
 * @param n Size of MY GROUP (number of elements I'm responsible for at this level)
 * @param level Recursion level (for switch generation)
 * @param rng RNG state
 */
void waksman_recursive(
    entry_t* array,
    size_t start,
    size_t stride,
    size_t n,
    uint32_t level,
    ShuffleRNG* rng
) {
    DEBUG_TRACE("waksman_recursive ENTER: start=%zu, stride=%zu, n=%zu, level=%u", 
                start, stride, n, level);
    
    // Base cases
    if (n <= 1) {
        DEBUG_TRACE("Base case n<=1, returning");
        return;  // Nothing to shuffle
    }
    
    if (n == 2) {
        // Single switch
        uint8_t swap = get_switch_bit(rng, level, start);
        DEBUG_TRACE("Base case n=2: swap=%d at positions %zu,%zu", 
                    swap, start, start + stride);
        oblivious_swap(&array[start], &array[start + stride], swap);
        return;
    }
    
    // For n > 2: Waksman recursive structure
    // REQUIRES: n is a power of 2 (enforced by padding)
    size_t half = n / 2;
    DEBUG_TRACE("Recursive case n=%zu (power of 2), half=%zu", n, half);
    
    // Input switches (one per pair)
    DEBUG_TRACE("Applying %zu input switches", half);
    for (size_t i = 0; i < half; i++) {
        size_t idx1 = start + (i * 2) * stride;
        size_t idx2 = start + (i * 2 + 1) * stride;
        
        // Add bounds check for debugging
        if (idx1 >= MAX_BATCH_SIZE || idx2 >= MAX_BATCH_SIZE) {
            DEBUG_ERROR("BOUNDS ERROR: idx1=%zu, idx2=%zu exceed MAX_BATCH_SIZE=%d", 
                       idx1, idx2, MAX_BATCH_SIZE);
            return;
        }
        
        uint8_t swap = get_switch_bit(rng, level, idx1);
        DEBUG_TRACE("  Input switch %zu: swap=%d at positions %zu,%zu", 
                    i, swap, idx1, idx2);
        oblivious_swap(&array[idx1], &array[idx2], swap);
    }
    
    // Recursive calls on interleaved positions
    // Both subnetworks get exactly n/2 elements (since n is power of 2)
    DEBUG_TRACE("Recursive call TOP: start=%zu, stride=%zu, n=%zu", 
                start, stride * 2, half);
    waksman_recursive(array, start, stride * 2, half, level + 1, rng);
    
    DEBUG_TRACE("Recursive call BOTTOM: start=%zu, stride=%zu, n=%zu", 
                start + stride, stride * 2, half);
    waksman_recursive(array, start + stride, stride * 2, half, level + 1, rng);
    
    // Output switches (one less than input for Waksman property)
    // First pair has no output switch
    size_t num_output_switches = (half > 1) ? half - 1 : 0;
    DEBUG_TRACE("Applying %zu output switches", num_output_switches);
    for (size_t i = 1; i <= num_output_switches; i++) {
        size_t idx1 = start + (i * 2) * stride;
        size_t idx2 = start + (i * 2 + 1) * stride;
        
        // Add bounds check for debugging
        if (idx1 >= MAX_BATCH_SIZE || idx2 >= MAX_BATCH_SIZE) {
            DEBUG_ERROR("BOUNDS ERROR in output: idx1=%zu, idx2=%zu exceed MAX_BATCH_SIZE=%d", 
                       idx1, idx2, MAX_BATCH_SIZE);
            return;
        }
        
        // Use different level offset to ensure different bits
        uint8_t swap = get_switch_bit(rng, level + 10000, idx1);
        DEBUG_TRACE("  Output switch %zu: swap=%d at positions %zu,%zu", 
                    i - 1, swap, idx1, idx2);
        oblivious_swap(&array[idx1], &array[idx2], swap);
    }
    
    DEBUG_TRACE("waksman_recursive EXIT: start=%zu, stride=%zu, n=%zu", 
                start, stride, n);
}

/**
 * Main entry point for oblivious 2-way Waksman shuffle
 * Shuffles n entries in-place
 * REQUIRES: n must be a power of 2 (enforced by caller via padding)
 */
int oblivious_2way_waksman(entry_t* data, size_t n) {
    DEBUG_INFO("=== oblivious_2way_waksman START: n=%zu ===", n);

    // Validate inputs
    if (!data || n == 0) {
        DEBUG_ERROR("Invalid parameters: data=%p, n=%zu", data, n);
        return -1;
    }

    if (n > MAX_BATCH_SIZE) {
        DEBUG_ERROR("Array too large: n=%zu > MAX_BATCH_SIZE=%d", n, MAX_BATCH_SIZE);
        return -1;  // Too large for in-memory shuffle
    }

    // Check that n is a power of 2
    if ((n & (n - 1)) != 0) {
        DEBUG_ERROR("n=%zu is not a power of 2. Padding required.", n);
        return -1;
    }

    DEBUG_TRACE("Initializing RNG");
    // Initialize RNG for this shuffle
    ShuffleRNG rng;
    init_shuffle_rng(&rng);

    DEBUG_INFO("Starting Waksman shuffle: n=%zu", n);
    // Apply Waksman shuffle
    waksman_recursive(data, 0, 1, n, 0, &rng);

    DEBUG_INFO("=== oblivious_2way_waksman END: SUCCESS ===");
    return 0;
}