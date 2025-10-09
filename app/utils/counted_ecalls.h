#ifndef COUNTED_ECALLS_H
#define COUNTED_ECALLS_H

/*
 * This header provides counted wrappers for the essential ecalls.
 *
 * After batching optimization, we've reduced from 40+ ecalls to just 4:
 * 1. encrypt_entry - For file I/O and debug
 * 2. decrypt_entry - For file I/O and debug
 * 3. obtain_output_size - Get output size
 * 4. batch_dispatcher - Handles all 36+ batched operations
 *
 * IMPORTANT: Always include this header instead of Enclave_u.h when making ecalls.
 * This ensures all ecalls are properly counted for performance monitoring.
 */

#include "sgx_compat/Enclave_u.h"
#include "../batch/ecall_wrapper.h"

// Counted wrapper functions for the essential ecalls
// These automatically increment the global ecall counter

inline crypto_status_t counted_ecall_encrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    (void)eid;  // Not used in compat layer
    crypto_status_t status = aes_encrypt_entry(entry);
    if (retval) *retval = status;
    if (status == CRYPTO_SUCCESS || status == CRYPTO_ALREADY_ENCRYPTED) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline crypto_status_t counted_ecall_decrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    (void)eid;  // Not used in compat layer
    crypto_status_t status = aes_decrypt_entry(entry);
    if (retval) *retval = status;
    if (status == CRYPTO_SUCCESS || status == CRYPTO_NOT_ENCRYPTED) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_obtain_output_size(sgx_enclave_id_t eid, int32_t* retval, const entry_t* entry) {
    (void)eid;  // Not used in compat layer
    int32_t size = obtain_output_size(entry);
    if (retval) *retval = size;
    g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    return SGX_SUCCESS;
}

inline sgx_status_t counted_ecall_batch_dispatcher(sgx_enclave_id_t eid, entry_t* data_array, size_t data_count,
                                                   void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type) {
    (void)eid;  // Not used in compat layer
    ecall_batch_dispatcher(data_array, data_count, ops_array, ops_count, ops_size, op_type);
    g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    return SGX_SUCCESS;
}

// Counted wrappers for heap sort and k-way merge (used by MergeSortManager)
inline sgx_status_t counted_ecall_heap_sort(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* array,
                                           size_t size, int comparator_type) {
    (void)eid;  // Not used in compat layer
    sgx_status_t status = ecall_heap_sort(array, size, comparator_type);
    if (retval) *retval = status;
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_merge_init(sgx_enclave_id_t eid, sgx_status_t* retval,
                                                   size_t k, int comparator_type) {
    (void)eid;  // Not used in compat layer
    sgx_status_t status = ecall_k_way_merge_init(k, comparator_type);
    if (retval) *retval = status;
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_merge_process(sgx_enclave_id_t eid, sgx_status_t* retval,
                                                      entry_t* output, size_t output_capacity,
                                                      size_t* output_produced, int* merge_complete) {
    (void)eid;  // Not used in compat layer
    sgx_status_t status = ecall_k_way_merge_process(output, output_capacity, output_produced, merge_complete);
    if (retval) *retval = status;
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_merge_cleanup(sgx_enclave_id_t eid, sgx_status_t* retval) {
    (void)eid;  // Not used in compat layer
    sgx_status_t status = ecall_k_way_merge_cleanup();
    if (retval) *retval = status;
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// Counted wrappers for shuffle ecalls (used by ShuffleManager)
inline sgx_status_t counted_ecall_oblivious_2way_waksman(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* data, size_t n) {
    (void)eid;  // Not used in compat layer
    sgx_status_t status = ecall_oblivious_2way_waksman(data, n);
    if (retval) *retval = status;
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_shuffle_decompose(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* input, size_t n) {
    (void)eid;  // Not used in compat layer
    sgx_status_t status = ecall_k_way_shuffle_decompose(input, n);
    if (retval) *retval = status;
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_shuffle_reconstruct(sgx_enclave_id_t eid, sgx_status_t* retval, size_t n) {
    (void)eid;  // Not used in compat layer
    sgx_status_t status = ecall_k_way_shuffle_reconstruct(n);
    if (retval) *retval = status;
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// For convenience, provide a macro that can be used for any ecall
#define COUNTED_ECALL(func_name, ...) \
    ({ \
        sgx_status_t _status = func_name(__VA_ARGS__); \
        if (_status == SGX_SUCCESS) { \
            g_ecall_count.fetch_add(1, std::memory_order_relaxed); \
        } \
        _status; \
    })

#endif // COUNTED_ECALLS_H
