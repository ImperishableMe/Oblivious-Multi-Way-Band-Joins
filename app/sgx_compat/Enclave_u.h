#ifndef ENCLAVE_U_H__
#define ENCLAVE_U_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include <string.h>
#include "sgx_types.h"
#include "../../common/enclave_types.h"
#include "../../common/entry_crypto.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Ocall Declarations
 * ============================================================================ */

void ocall_debug_print(uint32_t level, const char* file, int line, const char* message);
void ocall_refill_buffer(int buffer_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled);
void ocall_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size);
void ocall_refill_from_group(int group_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled);
void ocall_flush_output(entry_t* buffer, size_t buffer_size);

/* ============================================================================
 * Ecall Declarations - Direct implementations (no SGX wrappers needed)
 * ============================================================================ */

// Crypto ecalls
crypto_status_t aes_encrypt_entry(entry_t* entry);
crypto_status_t aes_decrypt_entry(entry_t* entry);

// Utility ecalls
int32_t obtain_output_size(const entry_t* last_entry);

// Batch dispatcher
void ecall_batch_dispatcher(entry_t* data_array, size_t data_count,
                           void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type);

// Sorting ecalls
sgx_status_t ecall_heap_sort(entry_t* array, size_t size, int comparator_type);

// K-way merge ecalls
sgx_status_t ecall_k_way_merge_init(size_t k, int comparator_type);
sgx_status_t ecall_k_way_merge_process(entry_t* output, size_t output_capacity,
                                       size_t* output_produced, int* merge_complete);
sgx_status_t ecall_k_way_merge_cleanup(void);

// Shuffle ecalls
sgx_status_t ecall_oblivious_2way_waksman(entry_t* data, size_t n);
sgx_status_t ecall_k_way_shuffle_decompose(entry_t* input, size_t n);
sgx_status_t ecall_k_way_shuffle_reconstruct(size_t n);

/* ============================================================================
 * Test Ecalls
 * ============================================================================ */

void ecall_test_noop(void);
void ecall_test_noop_small(void* data, size_t size);
void ecall_test_noop_inout(void* data, size_t size);
void ecall_test_noop_entries(entry_t* entries, size_t count);
void ecall_test_touch_entries(entry_t* entries, size_t count);
void ecall_test_increment_entries(entry_t* entries, size_t count);
void ecall_test_decrypt_only(entry_t* entries, size_t count);
void ecall_test_encrypt_only(entry_t* entries, size_t count);
void ecall_test_decrypt_and_compare(entry_t* entries, size_t count);
void ecall_test_compare_only(entry_t* entries, size_t count);
void ecall_test_full_cycle(entry_t* entries, size_t count);
void ecall_test_mixed_encryption(entry_t* entries, size_t count, int32_t encrypt_percent);

#ifdef __cplusplus
}
#endif

#endif /* ENCLAVE_U_H__ */
