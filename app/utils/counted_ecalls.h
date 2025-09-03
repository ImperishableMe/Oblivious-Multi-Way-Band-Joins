#ifndef COUNTED_ECALLS_H
#define COUNTED_ECALLS_H

/*
 * This header provides counted wrappers for the 4 essential ecalls.
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

#include "Enclave_u.h"
#include "../batch/ecall_wrapper.h"

// Counted wrapper functions for the 4 essential ecalls
// These automatically increment the global ecall counter

inline sgx_status_t counted_ecall_encrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    sgx_status_t status = ecall_encrypt_entry(eid, retval, entry);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_decrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    sgx_status_t status = ecall_decrypt_entry(eid, retval, entry);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_obtain_output_size(sgx_enclave_id_t eid, int32_t* retval, const entry_t* entry) {
    sgx_status_t status = ecall_obtain_output_size(eid, retval, entry);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_batch_dispatcher(sgx_enclave_id_t eid, entry_t* data_array, size_t data_count,
                                                   void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type) {
    sgx_status_t status = ecall_batch_dispatcher(eid, data_array, data_count, ops_array, ops_count, ops_size, op_type);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// Counted wrappers for k-way shuffle ecalls (used by ShuffleManager)
inline sgx_status_t counted_ecall_k_way_shuffle_decompose(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* input, size_t n) {
    sgx_status_t status = ecall_k_way_shuffle_decompose(eid, retval, input, n);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_shuffle_reconstruct(sgx_enclave_id_t eid, sgx_status_t* retval, size_t n) {
    sgx_status_t status = ecall_k_way_shuffle_reconstruct(eid, retval, n);
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