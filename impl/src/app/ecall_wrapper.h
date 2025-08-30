#ifndef ECALL_WRAPPER_H
#define ECALL_WRAPPER_H

#include "sgx_urts.h"
#include "Enclave_u.h"
#include <atomic>

// Global ecall counter
extern std::atomic<size_t> g_ecall_count;

// Counter management functions
void reset_ecall_count();
size_t get_ecall_count();

// Wrapper macro for ecalls that automatically increments counter
#define COUNTED_ECALL(func_name, ...) \
    ({ \
        sgx_status_t _status = func_name(__VA_ARGS__); \
        if (_status == SGX_SUCCESS) { \
            g_ecall_count.fetch_add(1, std::memory_order_relaxed); \
        } \
        _status; \
    })

// For backwards compatibility - these can be used directly
inline sgx_status_t counted_ecall_transform_set_index(sgx_enclave_id_t eid, entry_t* entry, uint32_t index) {
    sgx_status_t status = ecall_transform_set_index(eid, entry, index);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_obtain_output_size(sgx_enclave_id_t eid, int32_t* retval, const entry_t* last_entry) {
    sgx_status_t status = ecall_obtain_output_size(eid, retval, last_entry);
    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

#endif // ECALL_WRAPPER_H