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

void ecall_transform_initialize_leaf(entry_t* entry) {
    transform_initialize_leaf(entry);
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

void ecall_transform_to_source(entry_t* entry) {
    transform_to_source(entry);
}

void ecall_transform_to_start(entry_t* entry, int32_t deviation, equality_type_t equality) {
    transform_to_start(entry, deviation, equality);
}

void ecall_transform_to_end(entry_t* entry, int32_t deviation, equality_type_t equality) {
    transform_to_end(entry, deviation, equality);
}

void ecall_transform_set_empty(entry_t* entry) {
    transform_set_empty(entry);
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