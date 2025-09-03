/**
 * K-way Shuffle Implementation for Large Vectors
 * 
 * Implements k-way decomposition and reconstruction for shuffling
 * vectors larger than MAX_BATCH_SIZE using recursive structure.
 */

#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "../crypto/crypto_helpers.h"
#include "../../common/constants.h"
#include "../../common/debug_util.h"
#include "../Enclave_t.h"
#include <string.h>
#include <stdint.h>

// Forward declaration of waksman_recursive from oblivious_waksman.c
// We need to match the exact signature
typedef struct {
    uint64_t shuffle_nonce;
} ShuffleRNG;

// Function to initialize RNG (from oblivious_waksman.c)
static void init_shuffle_rng_local(ShuffleRNG* rng) {
    // Ensure AES key is initialized
    if (!aes_key_initialized) {
        init_aes_key();
    }
    // Get unique nonce from global counter
    rng->shuffle_nonce = get_next_nonce();
}

// Function to call waksman_recursive (defined in oblivious_waksman.c)
extern void waksman_recursive(entry_t* array, size_t start, size_t stride, 
                              size_t n, uint32_t level, ShuffleRNG* rng);

/**
 * K-way shuffle decomposition
 * Decomposes input of size n into k groups using oblivious shuffling
 * Each group of k consecutive elements is shuffled and distributed
 */
sgx_status_t ecall_k_way_shuffle_decompose(entry_t* input, size_t n) {
    const size_t k = MERGE_SORT_K;  // Use same K as merge sort (8)
    
    DEBUG_INFO("K-way decompose: n=%zu, k=%zu", n, k);
    
    // Verify n is multiple of k
    if (n % k != 0) {
        DEBUG_ERROR("n=%zu is not multiple of k=%zu", n, k);
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    // Decrypt input entries
    for (size_t i = 0; i < n; i++) {
        if (input[i].is_encrypted) {
            crypto_status_t status = aes_decrypt_entry(&input[i]);
            if (status != CRYPTO_SUCCESS) {
                // Re-encrypt already processed entries
                for (size_t j = 0; j < i; j++) {
                    aes_encrypt_entry(&input[j]);
                }
                return SGX_ERROR_UNEXPECTED;
            }
        }
    }
    
    size_t rounds = n / k;
    
    // Initialize RNG for shuffling
    ShuffleRNG rng;
    init_shuffle_rng_local(&rng);
    
    // Process k elements at a time
    for (size_t round = 0; round < rounds; round++) {
        entry_t temp[MERGE_SORT_K];
        
        DEBUG_TRACE("Processing round %zu/%zu", round + 1, rounds);
        
        // Copy k elements to temp buffer
        for (size_t i = 0; i < k; i++) {
            temp[i] = input[round * k + i];
        }
        
        // Shuffle these k elements using waksman_recursive directly
        // Since k=8 is power of 2, no padding needed
        waksman_recursive(temp, 0, 1, k, (uint32_t)round, &rng);
        
        // Send element i to group i via ocall
        for (size_t i = 0; i < k; i++) {
            // Re-encrypt before sending out
            crypto_status_t status = aes_encrypt_entry(&temp[i]);
            if (status != CRYPTO_SUCCESS && status != CRYPTO_ALREADY_ENCRYPTED) {
                DEBUG_ERROR("Failed to encrypt entry before ocall");
                return SGX_ERROR_UNEXPECTED;
            }
            
            // Ocall to append this entry to group i
            ocall_append_to_group((int)i, &temp[i]);
        }
    }
    
    DEBUG_INFO("K-way decompose complete: processed %zu rounds", rounds);
    return SGX_SUCCESS;
}

/**
 * K-way shuffle reconstruction
 * Reconstructs shuffled output from k groups
 * Collects one element from each group, shuffles them, and outputs
 */
sgx_status_t ecall_k_way_shuffle_reconstruct(size_t n) {
    const size_t k = MERGE_SORT_K;
    
    DEBUG_INFO("K-way reconstruct: n=%zu, k=%zu", n, k);
    
    if (n % k != 0) {
        DEBUG_ERROR("n=%zu is not multiple of k=%zu", n, k);
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    size_t rounds = n / k;
    
    // Initialize RNG
    ShuffleRNG rng;
    init_shuffle_rng_local(&rng);
    
    size_t output_pos = 0;
    
    for (size_t round = 0; round < rounds; round++) {
        entry_t temp[MERGE_SORT_K];
        
        DEBUG_TRACE("Reconstruction round %zu/%zu", round + 1, rounds);
        
        // Collect one element from each group via ocall
        for (size_t i = 0; i < k; i++) {
            ocall_get_from_group((int)i, &temp[i], round);
            
            // Decrypt for shuffling
            if (temp[i].is_encrypted) {
                crypto_status_t status = aes_decrypt_entry(&temp[i]);
                if (status != CRYPTO_SUCCESS) {
                    DEBUG_ERROR("Failed to decrypt entry from group %zu", i);
                    return SGX_ERROR_UNEXPECTED;
                }
            }
        }
        
        // Shuffle using waksman_recursive
        // Use different level offset to ensure different permutation than decomposition
        waksman_recursive(temp, 0, 1, k, (uint32_t)(round + 100000), &rng);
        
        // Output shuffled elements
        for (size_t i = 0; i < k; i++) {
            // Re-encrypt before output
            crypto_status_t status = aes_encrypt_entry(&temp[i]);
            if (status != CRYPTO_SUCCESS && status != CRYPTO_ALREADY_ENCRYPTED) {
                DEBUG_ERROR("Failed to encrypt entry for output");
                return SGX_ERROR_UNEXPECTED;
            }
            
            // Ocall to output this element
            ocall_output_element(&temp[i], output_pos++);
        }
    }
    
    DEBUG_INFO("K-way reconstruct complete: output %zu elements", output_pos);
    return SGX_SUCCESS;
}