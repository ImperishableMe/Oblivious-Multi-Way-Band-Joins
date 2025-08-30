#ifndef COUNTED_ECALLS_H
#define COUNTED_ECALLS_H

/*
 * This header provides counted wrappers for ALL ecalls.
 * 
 * IMPORTANT: Always include this header instead of Enclave_u.h when making ecalls.
 * This ensures all ecalls are properly counted for performance monitoring.
 * 
 * The actual Enclave_u.h is included here but its direct ecall functions
 * should NOT be used - use the counted_ versions instead.
 */

#include "Enclave_u.h"
#include "ecall_wrapper.h"

// Hide direct ecall access by defining them away when this header is included
// This will cause compilation errors if anyone tries to use direct ecalls
#ifdef ENFORCE_ECALL_COUNTING
  #define ecall_transform_set_index         DO_NOT_USE_DIRECT_ECALL_USE_counted_ecall_transform_set_index
  #define ecall_obtain_output_size          DO_NOT_USE_DIRECT_ECALL_USE_counted_ecall_obtain_output_size
  #define ecall_batch_dispatcher            DO_NOT_USE_DIRECT_ECALL_USE_counted_ecall_batch_dispatcher
  #define ecall_decrypt_entry               DO_NOT_USE_DIRECT_ECALL_USE_counted_ecall_decrypt_entry
  #define ecall_encrypt_entry               DO_NOT_USE_DIRECT_ECALL_USE_counted_ecall_encrypt_entry
  // Add more as needed
#endif

// Counted wrapper functions for all ecalls
// These automatically increment the global ecall counter
// Note: counted_ecall_transform_set_index and counted_ecall_obtain_output_size 
// are already defined in ecall_wrapper.h

inline sgx_status_t counted_ecall_batch_dispatcher(sgx_enclave_id_t eid, entry_t* data_array, size_t data_count,
                                                   void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type) {
    sgx_status_t status = ecall_batch_dispatcher(eid, data_array, data_count, ops_array, ops_count, ops_size, op_type);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// Note: ecall_create_padding_entry doesn't exist - padding is created through batch operations

inline sgx_status_t counted_ecall_decrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    sgx_status_t status = ecall_decrypt_entry(eid, retval, entry);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_encrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    sgx_status_t status = ecall_encrypt_entry(eid, retval, entry);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// Add more counted wrappers as needed for other ecalls...

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