#include "Enclave_t.h"

#include "sgx_trts.h" /* for sgx_ocalloc, sgx_is_outside_enclave */
#include "sgx_lfence.h" /* for sgx_lfence */

#include <errno.h>
#include <mbusafecrt.h> /* for memcpy_s etc */
#include <stdlib.h> /* for malloc/free etc */

#define CHECK_REF_POINTER(ptr, siz) do {	\
	if (!(ptr) || ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_UNIQUE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_ENCLAVE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_within_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define ADD_ASSIGN_OVERFLOW(a, b) (	\
	((a) += (b)) < (b)	\
)


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

static sgx_status_t SGX_CDECL sgx_ecall_encrypt_entry(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_encrypt_entry_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_encrypt_entry_t* ms = SGX_CAST(ms_ecall_encrypt_entry_t*, pms);
	ms_ecall_encrypt_entry_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_encrypt_entry_t), ms, sizeof(ms_ecall_encrypt_entry_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entry = __in_ms.ms_entry;
	size_t _len_entry = sizeof(entry_t);
	entry_t* _in_entry = NULL;
	crypto_status_t _in_retval;

	CHECK_UNIQUE_POINTER(_tmp_entry, _len_entry);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entry != NULL && _len_entry != 0) {
		_in_entry = (entry_t*)malloc(_len_entry);
		if (_in_entry == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entry, _len_entry, _tmp_entry, _len_entry)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	_in_retval = ecall_encrypt_entry(_in_entry);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}
	if (_in_entry) {
		if (memcpy_verw_s(_tmp_entry, _len_entry, _in_entry, _len_entry)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entry) free(_in_entry);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_decrypt_entry(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_decrypt_entry_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_decrypt_entry_t* ms = SGX_CAST(ms_ecall_decrypt_entry_t*, pms);
	ms_ecall_decrypt_entry_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_decrypt_entry_t), ms, sizeof(ms_ecall_decrypt_entry_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entry = __in_ms.ms_entry;
	size_t _len_entry = sizeof(entry_t);
	entry_t* _in_entry = NULL;
	crypto_status_t _in_retval;

	CHECK_UNIQUE_POINTER(_tmp_entry, _len_entry);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entry != NULL && _len_entry != 0) {
		_in_entry = (entry_t*)malloc(_len_entry);
		if (_in_entry == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entry, _len_entry, _tmp_entry, _len_entry)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	_in_retval = ecall_decrypt_entry(_in_entry);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}
	if (_in_entry) {
		if (memcpy_verw_s(_tmp_entry, _len_entry, _in_entry, _len_entry)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entry) free(_in_entry);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_obtain_output_size(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_obtain_output_size_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_obtain_output_size_t* ms = SGX_CAST(ms_ecall_obtain_output_size_t*, pms);
	ms_ecall_obtain_output_size_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_obtain_output_size_t), ms, sizeof(ms_ecall_obtain_output_size_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int32_t* _tmp_retval = __in_ms.ms_retval;
	size_t _len_retval = sizeof(int32_t);
	int32_t* _in_retval = NULL;
	const entry_t* _tmp_entry = __in_ms.ms_entry;
	size_t _len_entry = sizeof(entry_t);
	entry_t* _in_entry = NULL;

	CHECK_UNIQUE_POINTER(_tmp_retval, _len_retval);
	CHECK_UNIQUE_POINTER(_tmp_entry, _len_entry);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_retval != NULL && _len_retval != 0) {
		if ( _len_retval % sizeof(*_tmp_retval) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		if ((_in_retval = (int32_t*)malloc(_len_retval)) == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		memset((void*)_in_retval, 0, _len_retval);
	}
	if (_tmp_entry != NULL && _len_entry != 0) {
		_in_entry = (entry_t*)malloc(_len_entry);
		if (_in_entry == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entry, _len_entry, _tmp_entry, _len_entry)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_obtain_output_size(_in_retval, (const entry_t*)_in_entry);
	if (_in_retval) {
		if (memcpy_verw_s(_tmp_retval, _len_retval, _in_retval, _len_retval)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_retval) free(_in_retval);
	if (_in_entry) free(_in_entry);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_batch_dispatcher(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_batch_dispatcher_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_batch_dispatcher_t* ms = SGX_CAST(ms_ecall_batch_dispatcher_t*, pms);
	ms_ecall_batch_dispatcher_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_batch_dispatcher_t), ms, sizeof(ms_ecall_batch_dispatcher_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_data_array = __in_ms.ms_data_array;
	size_t _tmp_data_count = __in_ms.ms_data_count;
	size_t _len_data_array = _tmp_data_count * sizeof(entry_t);
	entry_t* _in_data_array = NULL;
	void* _tmp_ops_array = __in_ms.ms_ops_array;
	size_t _tmp_ops_size = __in_ms.ms_ops_size;
	size_t _len_ops_array = _tmp_ops_size;
	void* _in_ops_array = NULL;

	if (sizeof(*_tmp_data_array) != 0 &&
		(size_t)_tmp_data_count > (SIZE_MAX / sizeof(*_tmp_data_array))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_data_array, _len_data_array);
	CHECK_UNIQUE_POINTER(_tmp_ops_array, _len_ops_array);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_data_array != NULL && _len_data_array != 0) {
		_in_data_array = (entry_t*)malloc(_len_data_array);
		if (_in_data_array == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_data_array, _len_data_array, _tmp_data_array, _len_data_array)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	if (_tmp_ops_array != NULL && _len_ops_array != 0) {
		_in_ops_array = (void*)malloc(_len_ops_array);
		if (_in_ops_array == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_ops_array, _len_ops_array, _tmp_ops_array, _len_ops_array)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_batch_dispatcher(_in_data_array, _tmp_data_count, _in_ops_array, __in_ms.ms_ops_count, _tmp_ops_size, __in_ms.ms_op_type);
	if (_in_data_array) {
		if (memcpy_verw_s(_tmp_data_array, _len_data_array, _in_data_array, _len_data_array)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_data_array) free(_in_data_array);
	if (_in_ops_array) free(_in_ops_array);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_noop(void* pms)
{
	sgx_status_t status = SGX_SUCCESS;
	if (pms != NULL) return SGX_ERROR_INVALID_PARAMETER;
	ecall_test_noop();
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_noop_small(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_noop_small_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_noop_small_t* ms = SGX_CAST(ms_ecall_test_noop_small_t*, pms);
	ms_ecall_test_noop_small_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_noop_small_t), ms, sizeof(ms_ecall_test_noop_small_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	void* _tmp_data = __in_ms.ms_data;
	size_t _tmp_size = __in_ms.ms_size;
	size_t _len_data = _tmp_size;
	void* _in_data = NULL;

	CHECK_UNIQUE_POINTER(_tmp_data, _len_data);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_data != NULL && _len_data != 0) {
		_in_data = (void*)malloc(_len_data);
		if (_in_data == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_data, _len_data, _tmp_data, _len_data)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_noop_small(_in_data, _tmp_size);

err:
	if (_in_data) free(_in_data);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_noop_inout(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_noop_inout_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_noop_inout_t* ms = SGX_CAST(ms_ecall_test_noop_inout_t*, pms);
	ms_ecall_test_noop_inout_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_noop_inout_t), ms, sizeof(ms_ecall_test_noop_inout_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	void* _tmp_data = __in_ms.ms_data;
	size_t _tmp_size = __in_ms.ms_size;
	size_t _len_data = _tmp_size;
	void* _in_data = NULL;

	CHECK_UNIQUE_POINTER(_tmp_data, _len_data);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_data != NULL && _len_data != 0) {
		_in_data = (void*)malloc(_len_data);
		if (_in_data == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_data, _len_data, _tmp_data, _len_data)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_noop_inout(_in_data, _tmp_size);
	if (_in_data) {
		if (memcpy_verw_s(_tmp_data, _len_data, _in_data, _len_data)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_data) free(_in_data);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_noop_entries(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_noop_entries_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_noop_entries_t* ms = SGX_CAST(ms_ecall_test_noop_entries_t*, pms);
	ms_ecall_test_noop_entries_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_noop_entries_t), ms, sizeof(ms_ecall_test_noop_entries_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_noop_entries(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_sum_array(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_sum_array_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_sum_array_t* ms = SGX_CAST(ms_ecall_test_sum_array_t*, pms);
	ms_ecall_test_sum_array_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_sum_array_t), ms, sizeof(ms_ecall_test_sum_array_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	int32_t* _tmp_data = __in_ms.ms_data;
	size_t _tmp_size = __in_ms.ms_size;
	size_t _len_data = _tmp_size;
	int32_t* _in_data = NULL;
	int32_t _in_retval;

	CHECK_UNIQUE_POINTER(_tmp_data, _len_data);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_data != NULL && _len_data != 0) {
		if ( _len_data % sizeof(*_tmp_data) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_data = (int32_t*)malloc(_len_data);
		if (_in_data == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_data, _len_data, _tmp_data, _len_data)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	_in_retval = ecall_test_sum_array(_in_data, _tmp_size);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}

err:
	if (_in_data) free(_in_data);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_touch_entries(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_touch_entries_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_touch_entries_t* ms = SGX_CAST(ms_ecall_test_touch_entries_t*, pms);
	ms_ecall_test_touch_entries_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_touch_entries_t), ms, sizeof(ms_ecall_test_touch_entries_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_touch_entries(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_increment_entries(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_increment_entries_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_increment_entries_t* ms = SGX_CAST(ms_ecall_test_increment_entries_t*, pms);
	ms_ecall_test_increment_entries_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_increment_entries_t), ms, sizeof(ms_ecall_test_increment_entries_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_increment_entries(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_decrypt_only(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_decrypt_only_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_decrypt_only_t* ms = SGX_CAST(ms_ecall_test_decrypt_only_t*, pms);
	ms_ecall_test_decrypt_only_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_decrypt_only_t), ms, sizeof(ms_ecall_test_decrypt_only_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_decrypt_only(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_encrypt_only(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_encrypt_only_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_encrypt_only_t* ms = SGX_CAST(ms_ecall_test_encrypt_only_t*, pms);
	ms_ecall_test_encrypt_only_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_encrypt_only_t), ms, sizeof(ms_ecall_test_encrypt_only_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_encrypt_only(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_decrypt_and_compare(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_decrypt_and_compare_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_decrypt_and_compare_t* ms = SGX_CAST(ms_ecall_test_decrypt_and_compare_t*, pms);
	ms_ecall_test_decrypt_and_compare_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_decrypt_and_compare_t), ms, sizeof(ms_ecall_test_decrypt_and_compare_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_decrypt_and_compare(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_compare_only(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_compare_only_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_compare_only_t* ms = SGX_CAST(ms_ecall_test_compare_only_t*, pms);
	ms_ecall_test_compare_only_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_compare_only_t), ms, sizeof(ms_ecall_test_compare_only_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_compare_only(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_full_cycle(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_full_cycle_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_full_cycle_t* ms = SGX_CAST(ms_ecall_test_full_cycle_t*, pms);
	ms_ecall_test_full_cycle_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_full_cycle_t), ms, sizeof(ms_ecall_test_full_cycle_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_full_cycle(_in_entries, _tmp_count);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_test_mixed_encryption(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_test_mixed_encryption_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_test_mixed_encryption_t* ms = SGX_CAST(ms_ecall_test_mixed_encryption_t*, pms);
	ms_ecall_test_mixed_encryption_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_test_mixed_encryption_t), ms, sizeof(ms_ecall_test_mixed_encryption_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_entries = __in_ms.ms_entries;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_entries = _tmp_count * sizeof(entry_t);
	entry_t* _in_entries = NULL;

	if (sizeof(*_tmp_entries) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_entries))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_entries, _len_entries);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_entries != NULL && _len_entries != 0) {
		_in_entries = (entry_t*)malloc(_len_entries);
		if (_in_entries == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_entries, _len_entries, _tmp_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	ecall_test_mixed_encryption(_in_entries, _tmp_count, __in_ms.ms_encrypt_percent);
	if (_in_entries) {
		if (memcpy_verw_s(_tmp_entries, _len_entries, _in_entries, _len_entries)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_entries) free(_in_entries);
	return status;
}

SGX_EXTERNC const struct {
	size_t nr_ecall;
	struct {void* ecall_addr; uint8_t is_priv; uint8_t is_switchless;} ecall_table[17];
} g_ecall_table = {
	17,
	{
		{(void*)(uintptr_t)sgx_ecall_encrypt_entry, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_decrypt_entry, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_obtain_output_size, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_batch_dispatcher, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_noop, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_noop_small, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_noop_inout, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_noop_entries, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_sum_array, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_touch_entries, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_increment_entries, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_decrypt_only, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_encrypt_only, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_decrypt_and_compare, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_compare_only, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_full_cycle, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_test_mixed_encryption, 0, 0},
	}
};

SGX_EXTERNC const struct {
	size_t nr_ocall;
	uint8_t entry_table[6][17];
} g_dyn_entry_table = {
	6,
	{
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	}
};


sgx_status_t SGX_CDECL ocall_debug_print(uint32_t level, const char* file, int line, const char* message)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_file = file ? strlen(file) + 1 : 0;
	size_t _len_message = message ? strlen(message) + 1 : 0;

	ms_ocall_debug_print_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_debug_print_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(file, _len_file);
	CHECK_ENCLAVE_POINTER(message, _len_message);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (file != NULL) ? _len_file : 0))
		return SGX_ERROR_INVALID_PARAMETER;
	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (message != NULL) ? _len_message : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_debug_print_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_debug_print_t));
	ocalloc_size -= sizeof(ms_ocall_debug_print_t);

	if (memcpy_verw_s(&ms->ms_level, sizeof(ms->ms_level), &level, sizeof(level))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (file != NULL) {
		if (memcpy_verw_s(&ms->ms_file, sizeof(const char*), &__tmp, sizeof(const char*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_file % sizeof(*file) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, file, _len_file)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_file);
		ocalloc_size -= _len_file;
	} else {
		ms->ms_file = NULL;
	}

	if (memcpy_verw_s(&ms->ms_line, sizeof(ms->ms_line), &line, sizeof(line))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (message != NULL) {
		if (memcpy_verw_s(&ms->ms_message, sizeof(const char*), &__tmp, sizeof(const char*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_message % sizeof(*message) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, message, _len_message)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_message);
		ocalloc_size -= _len_message;
	} else {
		ms->ms_message = NULL;
	}

	status = sgx_ocall(0, ms);

	if (status == SGX_SUCCESS) {
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_cpuinfo = 4 * sizeof(int);

	ms_sgx_oc_cpuidex_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_oc_cpuidex_t);
	void *__tmp = NULL;

	void *__tmp_cpuinfo = NULL;

	CHECK_ENCLAVE_POINTER(cpuinfo, _len_cpuinfo);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (cpuinfo != NULL) ? _len_cpuinfo : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_oc_cpuidex_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_oc_cpuidex_t));
	ocalloc_size -= sizeof(ms_sgx_oc_cpuidex_t);

	if (cpuinfo != NULL) {
		if (memcpy_verw_s(&ms->ms_cpuinfo, sizeof(int*), &__tmp, sizeof(int*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp_cpuinfo = __tmp;
		if (_len_cpuinfo % sizeof(*cpuinfo) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		memset_verw(__tmp_cpuinfo, 0, _len_cpuinfo);
		__tmp = (void *)((size_t)__tmp + _len_cpuinfo);
		ocalloc_size -= _len_cpuinfo;
	} else {
		ms->ms_cpuinfo = NULL;
	}

	if (memcpy_verw_s(&ms->ms_leaf, sizeof(ms->ms_leaf), &leaf, sizeof(leaf))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_subleaf, sizeof(ms->ms_subleaf), &subleaf, sizeof(subleaf))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(1, ms);

	if (status == SGX_SUCCESS) {
		if (cpuinfo) {
			if (memcpy_s((void*)cpuinfo, _len_cpuinfo, __tmp_cpuinfo, _len_cpuinfo)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_wait_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);

	if (memcpy_verw_s(&ms->ms_self, sizeof(ms->ms_self), &self, sizeof(self))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(2, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_set_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(3, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_setwait_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_self, sizeof(ms->ms_self), &self, sizeof(self))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(4, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_waiters = total * sizeof(void*);

	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(waiters, _len_waiters);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (waiters != NULL) ? _len_waiters : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_multiple_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);

	if (waiters != NULL) {
		if (memcpy_verw_s(&ms->ms_waiters, sizeof(const void**), &__tmp, sizeof(const void**))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_waiters % sizeof(*waiters) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, waiters, _len_waiters)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_waiters);
		ocalloc_size -= _len_waiters;
	} else {
		ms->ms_waiters = NULL;
	}

	if (memcpy_verw_s(&ms->ms_total, sizeof(ms->ms_total), &total, sizeof(total))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(5, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

