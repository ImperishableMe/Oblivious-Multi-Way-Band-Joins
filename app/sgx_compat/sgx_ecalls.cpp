/* SGX Compatibility Layer - Ocall Implementations
 * Provides ocall implementations for the compat layer
 * Ecalls are implemented directly in enclave_logic/ and declared in Enclave_u.h
 */

#include "Enclave_u.h"
#include "sgx_types.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Ocall Implementations (Weak symbols - can be overridden)
 * ============================================================================ */

__attribute__((weak))
void ocall_debug_print(uint32_t level, const char* file, int line, const char* message) {
    // In compat mode, print directly to stderr
    fprintf(stderr, "[DEBUG L%u] %s:%d - %s\n", level, file, line, message);
}

__attribute__((weak))
void ocall_refill_buffer(int buffer_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled) {
    (void)buffer_idx;
    (void)buffer;
    (void)buffer_size;
    *actual_filled = 0;  // No refill in compat mode
}

__attribute__((weak))
void ocall_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size) {
    (void)group_idx;
    (void)buffer;
    (void)buffer_size;
    // No-op in compat mode
}

__attribute__((weak))
void ocall_refill_from_group(int group_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled) {
    (void)group_idx;
    (void)buffer;
    (void)buffer_size;
    *actual_filled = 0;  // No refill in compat mode
}

__attribute__((weak))
void ocall_flush_output(entry_t* buffer, size_t buffer_size) {
    (void)buffer;
    (void)buffer_size;
    // No-op in compat mode
}
