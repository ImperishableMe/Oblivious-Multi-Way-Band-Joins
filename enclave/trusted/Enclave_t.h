#ifndef ENCLAVE_T_H__
#define ENCLAVE_T_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include "sgx_edger8r.h" /* for sgx_ocall etc. */

#include "../../common/enclave_types.h"
#include "../../common/entry_crypto.h"
#include "enclave_types.h"

#include <stdlib.h> /* for size_t */

#define SGX_CAST(type, item) ((type)(item))

#ifdef __cplusplus
extern "C" {
#endif

crypto_status_t ecall_encrypt_entry(entry_t* entry);
crypto_status_t ecall_decrypt_entry(entry_t* entry);
void ecall_obtain_output_size(int32_t* retval, const entry_t* entry);
void ecall_batch_dispatcher(entry_t* data_array, size_t data_count, void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type);
sgx_status_t ecall_heap_sort(entry_t* array, size_t size, int comparator_type);
sgx_status_t ecall_k_way_merge_init(size_t k, int comparator_type);
sgx_status_t ecall_k_way_merge_process(entry_t* output, size_t output_capacity, size_t* output_produced, int* merge_complete);
sgx_status_t ecall_k_way_merge_cleanup(void);
void ecall_test_noop(void);
void ecall_test_noop_small(void* data, size_t size);
void ecall_test_noop_inout(void* data, size_t size);
void ecall_test_noop_entries(entry_t* entries, size_t count);
int32_t ecall_test_sum_array(int32_t* data, size_t size);
void ecall_test_touch_entries(entry_t* entries, size_t count);
void ecall_test_increment_entries(entry_t* entries, size_t count);
void ecall_test_decrypt_only(entry_t* entries, size_t count);
void ecall_test_encrypt_only(entry_t* entries, size_t count);
void ecall_test_decrypt_and_compare(entry_t* entries, size_t count);
void ecall_test_compare_only(entry_t* entries, size_t count);
void ecall_test_full_cycle(entry_t* entries, size_t count);
void ecall_test_mixed_encryption(entry_t* entries, size_t count, int32_t encrypt_percent);

sgx_status_t SGX_CDECL ocall_debug_print(uint32_t level, const char* file, int line, const char* message);
sgx_status_t SGX_CDECL ocall_refill_buffer(int buffer_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled);
sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf);
sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter);
sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
