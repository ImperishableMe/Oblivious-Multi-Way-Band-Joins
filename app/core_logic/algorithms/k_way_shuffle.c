/**
 * K-way Shuffle Implementation for Large Vectors (TDX)
 *
 * Implements k-way decomposition and reconstruction for shuffling
 * vectors larger than MAX_BATCH_SIZE using recursive structure.
 * Uses buffered I/O similar to k-way merge for efficiency.
 *
 * TDX migration: No encryption/decryption needed - data protected by VM
 */

#include "enclave_types.h"
#include "constants.h"
#include "debug_util.h"
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>

// Remove redefinition - use constant from constants.h
// Buffer size is defined in constants.h as (MAX_BATCH_SIZE / MERGE_SORT_K)

// Forward declaration of waksman_recursive from oblivious_waksman.c
typedef struct {
    uint64_t shuffle_seed;
} ShuffleRNG;

// Function to initialize RNG (TDX: hash-based, not AES-based)
static void init_shuffle_rng_local(ShuffleRNG* rng) {
    // Use time and random for seed (same approach as oblivious_waksman.c)
    rng->shuffle_seed = (uint64_t)time(NULL) ^ ((uint64_t)rand() << 32) ^ (uint64_t)rand();
}

// Forward declaration from oblivious_waksman.c
void waksman_recursive(entry_t* data, size_t start, size_t m, size_t n,
                       uint32_t level, ShuffleRNG* rng);

// External callbacks (direct function calls in TDX, not actual ocalls)
extern void ocall_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size);
extern void ocall_refill_from_group(int group_idx, entry_t* buffer,
                                    size_t buffer_size, size_t* actual_filled);
extern void ocall_flush_output(entry_t* buffer, size_t buffer_size);

/**
 * Shuffle state for buffered operations
 */
typedef struct {
    // Output buffers for decompose (k buffers, one per group)
    entry_t output_buffers[MERGE_SORT_K][MERGE_BUFFER_SIZE];
    size_t output_buffer_sizes[MERGE_SORT_K];  // Current fill level

    // Input buffers for reconstruct (k buffers, one per group)
    entry_t input_buffers[MERGE_SORT_K][MERGE_BUFFER_SIZE];
    size_t input_buffer_sizes[MERGE_SORT_K];   // Valid entries in buffer
    size_t input_buffer_pos[MERGE_SORT_K];     // Current read position

    // Track which round we're on for each group
    size_t group_rounds_processed[MERGE_SORT_K];

    // Common state
    size_t total_rounds;                       // Total rounds to process
    size_t current_round;                      // Current round being processed

    int decompose_initialized;                 // Decompose state initialized
    int reconstruct_initialized;               // Reconstruct state initialized
} ShuffleState;

// Global state
static ShuffleState g_shuffle_state = {0};

/**
 * Initialize decompose state
 */
static void init_decompose_state(size_t n) {
    memset(&g_shuffle_state, 0, sizeof(ShuffleState));
    g_shuffle_state.total_rounds = n / MERGE_SORT_K;
    g_shuffle_state.current_round = 0;
    g_shuffle_state.decompose_initialized = 1;

    DEBUG_INFO("Decompose state initialized: n=%zu, rounds=%zu", n, g_shuffle_state.total_rounds);
}

/**
 * Initialize reconstruct state
 */
static void init_reconstruct_state(size_t n) {
    memset(&g_shuffle_state, 0, sizeof(ShuffleState));
    g_shuffle_state.total_rounds = n / MERGE_SORT_K;
    g_shuffle_state.current_round = 0;
    g_shuffle_state.reconstruct_initialized = 1;

    // Initialize all input buffers as empty (need refill)
    for (size_t i = 0; i < MERGE_SORT_K; i++) {
        g_shuffle_state.input_buffer_sizes[i] = 0;
        g_shuffle_state.input_buffer_pos[i] = 0;
        g_shuffle_state.group_rounds_processed[i] = 0;
    }

    DEBUG_INFO("Reconstruct state initialized: n=%zu, rounds=%zu", n, g_shuffle_state.total_rounds);
}

/**
 * Flush output buffer for a specific group
 * TDX migration: No encryption needed
 */
static int flush_output_buffer(int group_idx) {
    if (g_shuffle_state.output_buffer_sizes[group_idx] == 0) {
        return 0;  // Nothing to flush
    }

    // Callback to flush this group's buffer (direct call in TDX)
    ocall_flush_to_group(group_idx,
                         g_shuffle_state.output_buffers[group_idx],
                         g_shuffle_state.output_buffer_sizes[group_idx]);

    // Reset buffer
    g_shuffle_state.output_buffer_sizes[group_idx] = 0;

    return 0;  // Success
}

/**
 * Refill input buffer for a specific group
 * TDX migration: No decryption needed
 */
static int refill_input_buffer(int group_idx) {
    size_t actual_filled = 0;

    // Request buffer refill via callback (direct call in TDX)
    ocall_refill_from_group(group_idx,
                            g_shuffle_state.input_buffers[group_idx],
                            MERGE_BUFFER_SIZE,
                            &actual_filled);

    if (actual_filled > 0) {
        // TDX: No decryption needed, data is protected by VM

        g_shuffle_state.input_buffer_sizes[group_idx] = actual_filled;
        g_shuffle_state.input_buffer_pos[group_idx] = 0;
    } else {
        g_shuffle_state.input_buffer_sizes[group_idx] = 0;
    }

    return 0;  // Success
}

/**
 * K-way shuffle decomposition
 * Takes n elements and distributes them into k groups
 * TDX migration: No encryption/decryption needed
 */
int k_way_shuffle_decompose(entry_t* input, size_t n) {
    const size_t k = MERGE_SORT_K;

    DEBUG_INFO("K-way decompose: n=%zu, k=%zu", n, k);

    // Verify n is multiple of k
    if (n % k != 0) {
        DEBUG_ERROR("n=%zu is not multiple of k=%zu", n, k);
        return -1;  // Invalid parameter
    }

    // Initialize state
    init_decompose_state(n);

    // TDX: No decryption needed, data is already accessible

    // Initialize RNG for shuffling
    ShuffleRNG rng;
    init_shuffle_rng_local(&rng);

    size_t rounds = n / k;
    entry_t temp[MERGE_SORT_K];

    // Process all rounds
    for (size_t round = 0; round < rounds; round++) {
        // Copy k elements to temp buffer
        for (size_t i = 0; i < k; i++) {
            temp[i] = input[round * k + i];
        }

        // Shuffle these k elements
        waksman_recursive(temp, 0, 1, k, (uint32_t)round, &rng);

        // Send element i to group i's output buffer
        for (size_t i = 0; i < k; i++) {
            // Add to output buffer
            size_t buf_pos = g_shuffle_state.output_buffer_sizes[i];
            g_shuffle_state.output_buffers[i][buf_pos] = temp[i];
            g_shuffle_state.output_buffer_sizes[i]++;

            // Flush if buffer is full
            if (g_shuffle_state.output_buffer_sizes[i] >= MERGE_BUFFER_SIZE) {
                int status = flush_output_buffer(i);
                if (status != 0) {
                    return status;
                }
            }
        }
    }

    // Flush any remaining data in output buffers
    for (size_t i = 0; i < k; i++) {
        int status = flush_output_buffer(i);
        if (status != 0) {
            return status;
        }
    }

    DEBUG_INFO("K-way decompose complete: processed %zu rounds", rounds);
    g_shuffle_state.decompose_initialized = 0;
    return 0;  // Success
}

/**
 * K-way shuffle reconstruction
 * Reconstructs shuffled output from k groups
 * TDX migration: No encryption/decryption needed
 */
int k_way_shuffle_reconstruct(size_t n) {
    const size_t k = MERGE_SORT_K;

    DEBUG_INFO("K-way reconstruct: n=%zu, k=%zu", n, k);

    if (n % k != 0) {
        DEBUG_ERROR("n=%zu is not multiple of k=%zu", n, k);
        return -1;  // Invalid parameter
    }

    // Initialize state
    init_reconstruct_state(n);

    // Initialize RNG
    ShuffleRNG rng;
    init_shuffle_rng_local(&rng);

    size_t rounds = n / k;
    entry_t temp[MERGE_SORT_K];
    entry_t output_buffer[MERGE_BUFFER_SIZE];
    size_t output_buffer_size = 0;

    for (size_t round = 0; round < rounds; round++) {
        // Collect one element from each group
        for (size_t i = 0; i < k; i++) {
            // Check if we need to refill this group's buffer
            if (g_shuffle_state.input_buffer_pos[i] >= g_shuffle_state.input_buffer_sizes[i]) {
                int status = refill_input_buffer(i);
                if (status != 0) {
                    return status;
                }

                // Check if refill succeeded
                if (g_shuffle_state.input_buffer_sizes[i] == 0) {
                    DEBUG_ERROR("Group %zu exhausted at round %zu", i, round);
                    return -1;
                }
            }

            // Get next element from group i
            temp[i] = g_shuffle_state.input_buffers[i][g_shuffle_state.input_buffer_pos[i]];
            g_shuffle_state.input_buffer_pos[i]++;
        }

        // Shuffle these k elements
        waksman_recursive(temp, 0, 1, k, (uint32_t)(round + 100000), &rng);

        // Add to output buffer (TDX: no encryption needed)
        for (size_t i = 0; i < k; i++) {
            output_buffer[output_buffer_size++] = temp[i];

            // Flush output buffer if full
            if (output_buffer_size >= MERGE_BUFFER_SIZE) {
                ocall_flush_output(output_buffer, output_buffer_size);
                output_buffer_size = 0;
            }
        }
    }

    // Flush any remaining output
    if (output_buffer_size > 0) {
        ocall_flush_output(output_buffer, output_buffer_size);
    }

    DEBUG_INFO("K-way reconstruct complete: processed %zu rounds", rounds);
    g_shuffle_state.reconstruct_initialized = 0;
    return 0;  // Success
}
