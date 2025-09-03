#ifndef ENCLAVE_U_H__
#define ENCLAVE_U_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include <string.h>
#include "sgx_edger8r.h" /* for sgx_status_t etc. */

#include "../../common/enclave_types.h"
#include "../../common/entry_crypto.h"
#include "enclave_types.h"

#include <stdlib.h> /* for size_t */

#define SGX_CAST(type, item) ((type)(item))

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OCALL_DEBUG_PRINT_DEFINED__
#define OCALL_DEBUG_PRINT_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_debug_print, (uint32_t level, const char* file, int line, const char* message));
#endif
#ifndef OCALL_REFILL_BUFFER_DEFINED__
#define OCALL_REFILL_BUFFER_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_refill_buffer, (int buffer_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled));
#endif
#ifndef SGX_OC_CPUIDEX_DEFINED__
#define SGX_OC_CPUIDEX_DEFINED__
void SGX_UBRIDGE(SGX_CDECL, sgx_oc_cpuidex, (int cpuinfo[4], int leaf, int subleaf));
#endif
#ifndef SGX_THREAD_WAIT_UNTRUSTED_EVENT_OCALL_DEFINED__
#define SGX_THREAD_WAIT_UNTRUSTED_EVENT_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_wait_untrusted_event_ocall, (const void* self));
#endif
#ifndef SGX_THREAD_SET_UNTRUSTED_EVENT_OCALL_DEFINED__
#define SGX_THREAD_SET_UNTRUSTED_EVENT_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_set_untrusted_event_ocall, (const void* waiter));
#endif
#ifndef SGX_THREAD_SETWAIT_UNTRUSTED_EVENTS_OCALL_DEFINED__
#define SGX_THREAD_SETWAIT_UNTRUSTED_EVENTS_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_setwait_untrusted_events_ocall, (const void* waiter, const void* self));
#endif
#ifndef SGX_THREAD_SET_MULTIPLE_UNTRUSTED_EVENTS_OCALL_DEFINED__
#define SGX_THREAD_SET_MULTIPLE_UNTRUSTED_EVENTS_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_set_multiple_untrusted_events_ocall, (const void** waiters, size_t total));
#endif

sgx_status_t ecall_encrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry);
sgx_status_t ecall_decrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry);
sgx_status_t ecall_obtain_output_size(sgx_enclave_id_t eid, int32_t* retval, const entry_t* entry);
sgx_status_t ecall_batch_dispatcher(sgx_enclave_id_t eid, entry_t* data_array, size_t data_count, void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type);
sgx_status_t ecall_heap_sort(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* array, size_t size, int comparator_type);
sgx_status_t ecall_k_way_merge_init(sgx_enclave_id_t eid, sgx_status_t* retval, size_t k, int comparator_type);
sgx_status_t ecall_k_way_merge_process(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* output, size_t output_capacity, size_t* output_produced, int* merge_complete);
sgx_status_t ecall_k_way_merge_cleanup(sgx_enclave_id_t eid, sgx_status_t* retval);
sgx_status_t ecall_test_noop(sgx_enclave_id_t eid);
sgx_status_t ecall_test_noop_small(sgx_enclave_id_t eid, void* data, size_t size);
sgx_status_t ecall_test_noop_inout(sgx_enclave_id_t eid, void* data, size_t size);
sgx_status_t ecall_test_noop_entries(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_sum_array(sgx_enclave_id_t eid, int32_t* retval, int32_t* data, size_t size);
sgx_status_t ecall_test_touch_entries(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_increment_entries(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_decrypt_only(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_encrypt_only(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_decrypt_and_compare(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_compare_only(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_full_cycle(sgx_enclave_id_t eid, entry_t* entries, size_t count);
sgx_status_t ecall_test_mixed_encryption(sgx_enclave_id_t eid, entry_t* entries, size_t count, int32_t encrypt_percent);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
