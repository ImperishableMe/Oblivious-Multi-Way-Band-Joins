#include "Enclave_u.h"
#include <errno.h>

typedef struct ms_ecall_encrypt_entry_t {
	crypto_status_t ms_retval;
	entry_t* ms_entry;
} ms_ecall_encrypt_entry_t;

typedef struct ms_ecall_decrypt_entry_t {
	crypto_status_t ms_retval;
	entry_t* ms_entry;
} ms_ecall_decrypt_entry_t;

typedef struct ms_ecall_obtain_output_size_t {
	int32_t* ms_retval;
	const entry_t* ms_entry;
} ms_ecall_obtain_output_size_t;

typedef struct ms_ecall_batch_dispatcher_t {
	entry_t* ms_data_array;
	size_t ms_data_count;
	void* ms_ops_array;
	size_t ms_ops_count;
	size_t ms_ops_size;
	int32_t ms_op_type;
} ms_ecall_batch_dispatcher_t;

typedef struct ms_ecall_test_noop_small_t {
	void* ms_data;
	size_t ms_size;
} ms_ecall_test_noop_small_t;

typedef struct ms_ecall_test_noop_inout_t {
	void* ms_data;
	size_t ms_size;
} ms_ecall_test_noop_inout_t;

typedef struct ms_ecall_test_noop_entries_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_noop_entries_t;

typedef struct ms_ecall_test_sum_array_t {
	int32_t ms_retval;
	int32_t* ms_data;
	size_t ms_size;
} ms_ecall_test_sum_array_t;

typedef struct ms_ecall_test_touch_entries_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_touch_entries_t;

typedef struct ms_ecall_test_increment_entries_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_increment_entries_t;

typedef struct ms_ecall_test_decrypt_only_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_decrypt_only_t;

typedef struct ms_ecall_test_encrypt_only_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_encrypt_only_t;

typedef struct ms_ecall_test_decrypt_and_compare_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_decrypt_and_compare_t;

typedef struct ms_ecall_test_compare_only_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_compare_only_t;

typedef struct ms_ecall_test_full_cycle_t {
	entry_t* ms_entries;
	size_t ms_count;
} ms_ecall_test_full_cycle_t;

typedef struct ms_ecall_test_mixed_encryption_t {
	entry_t* ms_entries;
	size_t ms_count;
	int32_t ms_encrypt_percent;
} ms_ecall_test_mixed_encryption_t;

typedef struct ms_ocall_debug_print_t {
	uint32_t ms_level;
	const char* ms_file;
	int ms_line;
	const char* ms_message;
} ms_ocall_debug_print_t;

typedef struct ms_sgx_oc_cpuidex_t {
	int* ms_cpuinfo;
	int ms_leaf;
	int ms_subleaf;
} ms_sgx_oc_cpuidex_t;

typedef struct ms_sgx_thread_wait_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_self;
} ms_sgx_thread_wait_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_set_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_waiter;
} ms_sgx_thread_set_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_setwait_untrusted_events_ocall_t {
	int ms_retval;
	const void* ms_waiter;
	const void* ms_self;
} ms_sgx_thread_setwait_untrusted_events_ocall_t;

typedef struct ms_sgx_thread_set_multiple_untrusted_events_ocall_t {
	int ms_retval;
	const void** ms_waiters;
	size_t ms_total;
} ms_sgx_thread_set_multiple_untrusted_events_ocall_t;

static sgx_status_t SGX_CDECL Enclave_ocall_debug_print(void* pms)
{
	ms_ocall_debug_print_t* ms = SGX_CAST(ms_ocall_debug_print_t*, pms);
	ocall_debug_print(ms->ms_level, ms->ms_file, ms->ms_line, ms->ms_message);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_oc_cpuidex(void* pms)
{
	ms_sgx_oc_cpuidex_t* ms = SGX_CAST(ms_sgx_oc_cpuidex_t*, pms);
	sgx_oc_cpuidex(ms->ms_cpuinfo, ms->ms_leaf, ms->ms_subleaf);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_wait_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_wait_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_wait_untrusted_event_ocall(ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_set_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_untrusted_event_ocall(ms->ms_waiter);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_setwait_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_setwait_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_setwait_untrusted_events_ocall(ms->ms_waiter, ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_multiple_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_multiple_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_multiple_untrusted_events_ocall(ms->ms_waiters, ms->ms_total);

	return SGX_SUCCESS;
}

static const struct {
	size_t nr_ocall;
	void * table[6];
} ocall_table_Enclave = {
	6,
	{
		(void*)Enclave_ocall_debug_print,
		(void*)Enclave_sgx_oc_cpuidex,
		(void*)Enclave_sgx_thread_wait_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_set_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_setwait_untrusted_events_ocall,
		(void*)Enclave_sgx_thread_set_multiple_untrusted_events_ocall,
	}
};
sgx_status_t ecall_encrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry)
{
	sgx_status_t status;
	ms_ecall_encrypt_entry_t ms;
	ms.ms_entry = entry;
	status = sgx_ecall(eid, 0, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_decrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry)
{
	sgx_status_t status;
	ms_ecall_decrypt_entry_t ms;
	ms.ms_entry = entry;
	status = sgx_ecall(eid, 1, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_obtain_output_size(sgx_enclave_id_t eid, int32_t* retval, const entry_t* entry)
{
	sgx_status_t status;
	ms_ecall_obtain_output_size_t ms;
	ms.ms_retval = retval;
	ms.ms_entry = entry;
	status = sgx_ecall(eid, 2, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_batch_dispatcher(sgx_enclave_id_t eid, entry_t* data_array, size_t data_count, void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type)
{
	sgx_status_t status;
	ms_ecall_batch_dispatcher_t ms;
	ms.ms_data_array = data_array;
	ms.ms_data_count = data_count;
	ms.ms_ops_array = ops_array;
	ms.ms_ops_count = ops_count;
	ms.ms_ops_size = ops_size;
	ms.ms_op_type = op_type;
	status = sgx_ecall(eid, 3, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_noop(sgx_enclave_id_t eid)
{
	sgx_status_t status;
	status = sgx_ecall(eid, 4, &ocall_table_Enclave, NULL);
	return status;
}

sgx_status_t ecall_test_noop_small(sgx_enclave_id_t eid, void* data, size_t size)
{
	sgx_status_t status;
	ms_ecall_test_noop_small_t ms;
	ms.ms_data = data;
	ms.ms_size = size;
	status = sgx_ecall(eid, 5, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_noop_inout(sgx_enclave_id_t eid, void* data, size_t size)
{
	sgx_status_t status;
	ms_ecall_test_noop_inout_t ms;
	ms.ms_data = data;
	ms.ms_size = size;
	status = sgx_ecall(eid, 6, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_noop_entries(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_noop_entries_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 7, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_sum_array(sgx_enclave_id_t eid, int32_t* retval, int32_t* data, size_t size)
{
	sgx_status_t status;
	ms_ecall_test_sum_array_t ms;
	ms.ms_data = data;
	ms.ms_size = size;
	status = sgx_ecall(eid, 8, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_test_touch_entries(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_touch_entries_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 9, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_increment_entries(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_increment_entries_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 10, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_decrypt_only(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_decrypt_only_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 11, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_encrypt_only(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_encrypt_only_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 12, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_decrypt_and_compare(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_decrypt_and_compare_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 13, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_compare_only(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_compare_only_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 14, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_full_cycle(sgx_enclave_id_t eid, entry_t* entries, size_t count)
{
	sgx_status_t status;
	ms_ecall_test_full_cycle_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	status = sgx_ecall(eid, 15, &ocall_table_Enclave, &ms);
	return status;
}

sgx_status_t ecall_test_mixed_encryption(sgx_enclave_id_t eid, entry_t* entries, size_t count, int32_t encrypt_percent)
{
	sgx_status_t status;
	ms_ecall_test_mixed_encryption_t ms;
	ms.ms_entries = entries;
	ms.ms_count = count;
	ms.ms_encrypt_percent = encrypt_percent;
	status = sgx_ecall(eid, 16, &ocall_table_Enclave, &ms);
	return status;
}

