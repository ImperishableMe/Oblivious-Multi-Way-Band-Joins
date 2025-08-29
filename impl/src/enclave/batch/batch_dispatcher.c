#include "batch_dispatcher.h"
#include "../core/core.h"
#include "../crypto/aes_crypto.h"
#include "../Enclave_t.h"

/**
 * Batch dispatcher implementation
 * Routes batched operations to the appropriate core functions
 * 
 * IMPORTANT: This dispatcher handles encryption/decryption at the batch level.
 * 1. Decrypts all entries on entry
 * 2. Performs all operations on plaintext
 * 3. Re-encrypts all entries on exit
 * This eliminates per-operation crypto overhead (up to 2000x reduction)
 */
void ecall_batch_dispatcher(entry_t* data_array, size_t data_count,
                           BatchOperation* ops_array, size_t ops_count,
                           OpEcall op_type) {
    
    // Validate inputs
    if (!data_array || !ops_array || data_count == 0 || ops_count == 0) {
        return;
    }
    
    // ============================================================================
    // BATCH DECRYPTION: Decrypt all entries at once
    // ============================================================================
    // Use static allocation for small batches to avoid malloc overhead
    static uint8_t was_encrypted_buffer[2048];  // Max batch size
    uint8_t* was_encrypted = NULL;
    
    if (data_count <= 2048) {
        was_encrypted = was_encrypted_buffer;
    } else {
        was_encrypted = (uint8_t*)malloc(data_count * sizeof(uint8_t));
        if (!was_encrypted) {
            return; // Memory allocation failed
        }
    }
    
    // Decrypt all entries and remember their original encryption state
    for (size_t i = 0; i < data_count; i++) {
        was_encrypted[i] = data_array[i].is_encrypted;
        if (was_encrypted[i]) {
            crypto_status_t status = aes_decrypt_entry(&data_array[i]);
            if (status != CRYPTO_SUCCESS) {
                // Failed to decrypt - cannot proceed
                // Re-encrypt any already processed entries before returning
                for (size_t j = 0; j < i; j++) {
                    if (was_encrypted[j]) {
                        aes_encrypt_entry(&data_array[j]);
                    }
                }
                free(was_encrypted);
                return;
            }
        }
    }
    
    // Dispatch based on operation type
    switch(op_type) {
        // ============================================================================
        // Comparator Operations (two parameters)
        // ============================================================================
        
        case OP_ECALL_COMPARATOR_JOIN_ATTR:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_join_attr(&data_array[ops_array[i].idx1],
                                       &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_COMPARATOR_PAIRWISE:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_pairwise(&data_array[ops_array[i].idx1],
                                      &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_COMPARATOR_END_FIRST:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_end_first(&data_array[ops_array[i].idx1],
                                       &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_COMPARATOR_JOIN_THEN_OTHER:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_join_then_other(&data_array[ops_array[i].idx1],
                                              &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_COMPARATOR_ORIGINAL_INDEX:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_original_index(&data_array[ops_array[i].idx1],
                                             &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_COMPARATOR_ALIGNMENT_KEY:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_alignment_key(&data_array[ops_array[i].idx1],
                                           &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_COMPARATOR_PADDING_LAST:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_padding_last(&data_array[ops_array[i].idx1],
                                          &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_COMPARATOR_DISTRIBUTE:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    comparator_distribute(&data_array[ops_array[i].idx1],
                                        &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        // ============================================================================
        // Window Operations (two parameters)
        // ============================================================================
        
        case OP_ECALL_WINDOW_SET_ORIGINAL_INDEX:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    window_set_original_index(&data_array[ops_array[i].idx1],
                                            &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_WINDOW_COMPUTE_LOCAL_SUM:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    window_compute_local_sum(&data_array[ops_array[i].idx1],
                                           &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_WINDOW_COMPUTE_LOCAL_INTERVAL:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    window_compute_local_interval(&data_array[ops_array[i].idx1],
                                                &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_WINDOW_COMPUTE_FOREIGN_SUM:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    window_compute_foreign_sum(&data_array[ops_array[i].idx1],
                                             &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_WINDOW_COMPUTE_FOREIGN_INTERVAL:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    window_compute_foreign_interval(&data_array[ops_array[i].idx1],
                                                  &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_WINDOW_PROPAGATE_FOREIGN_INTERVAL:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    window_propagate_foreign_interval(&data_array[ops_array[i].idx1],
                                                     &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        // ============================================================================
        // Update Operations (two parameters)
        // ============================================================================
        
        case OP_ECALL_UPDATE_TARGET_MULTIPLICITY:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    update_target_multiplicity(&data_array[ops_array[i].idx1],
                                             &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        case OP_ECALL_UPDATE_TARGET_FINAL_MULTIPLICITY:
            for (size_t i = 0; i < ops_count; i++) {
                if (ops_array[i].idx2 != BATCH_NO_PARAM) {
                    update_target_final_multiplicity(&data_array[ops_array[i].idx1],
                                                   &data_array[ops_array[i].idx2]);
                }
            }
            break;
            
        // ============================================================================
        // Transform Operations (single parameter)
        // ============================================================================
        
        case OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE:
            for (size_t i = 0; i < ops_count; i++) {
                transform_set_local_mult_one(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_ADD_METADATA:
            for (size_t i = 0; i < ops_count; i++) {
                transform_add_metadata(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_INIT_LOCAL_TEMPS:
            for (size_t i = 0; i < ops_count; i++) {
                transform_init_local_temps(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_INIT_FINAL_MULT:
            for (size_t i = 0; i < ops_count; i++) {
                transform_init_final_mult(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS:
            for (size_t i = 0; i < ops_count; i++) {
                transform_init_foreign_temps(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_TO_SOURCE:
            for (size_t i = 0; i < ops_count; i++) {
                transform_to_source(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_SET_SORT_PADDING:
            for (size_t i = 0; i < ops_count; i++) {
                transform_set_sort_padding(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_INIT_DST_IDX:
            for (size_t i = 0; i < ops_count; i++) {
                transform_init_dst_idx(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_INIT_INDEX:
            for (size_t i = 0; i < ops_count; i++) {
                transform_init_index(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING:
            for (size_t i = 0; i < ops_count; i++) {
                transform_mark_zero_mult_padding(&data_array[ops_array[i].idx1]);
            }
            break;
            
        case OP_ECALL_TRANSFORM_CREATE_DIST_PADDING:
            for (size_t i = 0; i < ops_count; i++) {
                transform_create_dist_padding(&data_array[ops_array[i].idx1]);
            }
            break;
            
        // ============================================================================
        // Transform Operations with parameters
        // ============================================================================
        
        case OP_ECALL_TRANSFORM_TO_START:
            // extra_param encodes both deviation and equality type
            // High 16 bits: deviation, Low 16 bits: equality_type
            for (size_t i = 0; i < ops_count; i++) {
                int32_t deviation = (int32_t)(ops_array[i].extra_param >> 16);
                equality_type_t equality = (equality_type_t)(ops_array[i].extra_param & 0xFFFF);
                transform_to_start(&data_array[ops_array[i].idx1], deviation, equality);
            }
            break;
            
        case OP_ECALL_TRANSFORM_TO_END:
            // extra_param encodes both deviation and equality type
            for (size_t i = 0; i < ops_count; i++) {
                int32_t deviation = (int32_t)(ops_array[i].extra_param >> 16);
                equality_type_t equality = (equality_type_t)(ops_array[i].extra_param & 0xFFFF);
                transform_to_end(&data_array[ops_array[i].idx1], deviation, equality);
            }
            break;
            
        case OP_ECALL_TRANSFORM_SET_INDEX:
            for (size_t i = 0; i < ops_count; i++) {
                transform_set_index(&data_array[ops_array[i].idx1], ops_array[i].extra_param);
            }
            break;
            
        case OP_ECALL_TRANSFORM_SET_JOIN_ATTR:
            for (size_t i = 0; i < ops_count; i++) {
                transform_set_join_attr(&data_array[ops_array[i].idx1], 
                                       (int32_t)ops_array[i].extra_param);
            }
            break;
            
        case OP_ECALL_INIT_METADATA_NULL:
            for (size_t i = 0; i < ops_count; i++) {
                transform_init_metadata_null(&data_array[ops_array[i].idx1], ops_array[i].extra_param);
            }
            break;
            
        default:
            // Unknown operation type - do nothing
            break;
    }
    
    // ============================================================================
    // BATCH RE-ENCRYPTION: Re-encrypt all entries that were originally encrypted
    // ============================================================================
    for (size_t i = 0; i < data_count; i++) {
        if (was_encrypted[i]) {
            aes_encrypt_entry(&data_array[i]);
        }
    }
    
    // Clean up (only free if we malloced)
    if (data_count > 2048) {
        free(was_encrypted);
    }
}