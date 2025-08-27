#include "Enclave_t.h"
#include "enclave_types.h"
#include "crypto/entry_crypto.h"
#include "crypto/aes_crypto.h"
#include "core/core.h"

// Define ENCLAVE_BUILD so secure_key.h knows we're in the enclave
#define ENCLAVE_BUILD
#include "secure_key.h"

/**
 * AES-CTR Encryption/Decryption Ecall Implementations
 * These functions use AES-CTR with secure key stored inside the enclave
 */

crypto_status_t ecall_encrypt_entry(entry_t* entry) {
    // Use AES-CTR encryption with secure enclave key
    return aes_encrypt_entry(entry);
}

crypto_status_t ecall_decrypt_entry(entry_t* entry) {
    // Use AES-CTR decryption with secure enclave key
    return aes_decrypt_entry(entry);
}

/**
 * Transform Function Ecall Implementations
 */

void ecall_transform_set_local_mult_one(entry_t* entry) {
    transform_set_local_mult_one(entry);
}

void ecall_transform_add_metadata(entry_t* entry) {
    transform_add_metadata(entry);
}

void ecall_transform_set_index(entry_t* entry, uint32_t index) {
    transform_set_index(entry, index);
}

void ecall_transform_init_local_temps(entry_t* entry) {
    transform_init_local_temps(entry);
}

void ecall_transform_init_final_mult(entry_t* entry) {
    transform_init_final_mult(entry);
}

void ecall_transform_init_foreign_temps(entry_t* entry) {
    transform_init_foreign_temps(entry);
}

void ecall_transform_to_source(entry_t* entry) {
    transform_to_source(entry);
}

void ecall_transform_to_start(entry_t* entry, int32_t deviation, equality_type_t equality) {
    transform_to_start(entry, deviation, equality);
}

void ecall_transform_to_end(entry_t* entry, int32_t deviation, equality_type_t equality) {
    transform_to_end(entry, deviation, equality);
}

void ecall_transform_set_sort_padding(entry_t* entry) {
    transform_set_sort_padding(entry);
}

void ecall_transform_set_join_attr(entry_t* entry, int32_t column_index) {
    transform_set_join_attr(entry, column_index);
}

/**
 * Window Function Ecall Implementations
 */

void ecall_window_set_original_index(entry_t* e1, entry_t* e2) {
    window_set_original_index(e1, e2);
}

void ecall_window_compute_local_sum(entry_t* e1, entry_t* e2) {
    window_compute_local_sum(e1, e2);
}

void ecall_window_compute_local_interval(entry_t* e1, entry_t* e2) {
    window_compute_local_interval(e1, e2);
}

void ecall_window_compute_foreign_sum(entry_t* e1, entry_t* e2) {
    window_compute_foreign_sum(e1, e2);
}

void ecall_window_compute_foreign_interval(entry_t* e1, entry_t* e2) {
    window_compute_foreign_interval(e1, e2);
}

void ecall_window_propagate_foreign_interval(entry_t* e1, entry_t* e2) {
    window_propagate_foreign_interval(e1, e2);
}

void ecall_update_target_multiplicity(entry_t* e1, entry_t* e2) {
    update_target_multiplicity(e1, e2);
}

void ecall_update_target_final_multiplicity(entry_t* e1, entry_t* e2) {
    update_target_final_multiplicity(e1, e2);
}

/**
 * Comparator Ecall Implementations
 */

void ecall_comparator_join_attr(entry_t* e1, entry_t* e2) {
    comparator_join_attr(e1, e2);
}

void ecall_comparator_pairwise(entry_t* e1, entry_t* e2) {
    comparator_pairwise(e1, e2);
}

void ecall_comparator_end_first(entry_t* e1, entry_t* e2) {
    comparator_end_first(e1, e2);
}

void ecall_comparator_join_then_other(entry_t* e1, entry_t* e2) {
    comparator_join_then_other(e1, e2);
}

void ecall_comparator_original_index(entry_t* e1, entry_t* e2) {
    comparator_original_index(e1, e2);
}

void ecall_comparator_alignment_key(entry_t* e1, entry_t* e2) {
    comparator_alignment_key(e1, e2);
}

// Distribute-expand phase functions

void ecall_transform_init_dst_idx(entry_t* entry) {
    transform_init_dst_idx(entry);
}

void ecall_transform_init_index(entry_t* entry) {
    transform_init_index(entry);
}

void ecall_transform_mark_zero_mult_padding(entry_t* entry) {
    transform_mark_zero_mult_padding(entry);
}

void ecall_transform_create_dist_padding(entry_t* entry) {
    transform_create_dist_padding(entry);
}

void ecall_window_compute_dst_idx(entry_t* e1, entry_t* e2) {
    window_compute_dst_idx(e1, e2);
}

void ecall_window_increment_index(entry_t* e1, entry_t* e2) {
    window_increment_index(e1, e2);
}

void ecall_window_expand_copy(entry_t* e1, entry_t* e2) {
    window_expand_copy(e1, e2);
}

void ecall_comparator_padding_last(entry_t* e1, entry_t* e2) {
    comparator_padding_last(e1, e2);
}

void ecall_comparator_distribute(entry_t* e1, entry_t* e2) {
    comparator_distribute(e1, e2);
}

void ecall_obtain_output_size(int32_t* retval, const entry_t* entry) {
    *retval = obtain_output_size(entry);
}

// ============================================================================
// Align-Concat Phase ECalls
// ============================================================================

void ecall_transform_init_copy_index(entry_t* entry) {
    transform_init_copy_index(entry);
}

void ecall_transform_compute_alignment_key(entry_t* entry) {
    transform_compute_alignment_key(entry);
}

void ecall_window_update_copy_index(entry_t* e1, entry_t* e2) {
    window_update_copy_index(e1, e2);
}