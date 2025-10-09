#include "enclave_types.h"
#include <cstddef>
#include <cstdint>

/**
 * Callback function stubs for TDX migration
 * These replace the SGX ocalls with direct function calls
 *
 * In TDX, these are called directly from the core_logic code (previously enclave code)
 * The actual implementations will be provided by the merge_sort_manager and shuffle_manager
 */

extern "C" {

// Callback for k-way merge to refill a buffer from a specific run
void ocall_refill_buffer(int buffer_idx, entry_t* buffer,
                        size_t buffer_size, size_t* actual_filled) {
    // TDX: Stub implementation - will be replaced by actual merge manager callback
    // For now, mark as exhausted
    *actual_filled = 0;
    (void)buffer_idx;
    (void)buffer;
    (void)buffer_size;
}

// Callback for k-way shuffle to flush data to a specific group
void ocall_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size) {
    // TDX: Stub implementation - will be replaced by actual shuffle manager callback
    (void)group_idx;
    (void)buffer;
    (void)buffer_size;
}

// Callback for k-way shuffle to refill buffer from a specific group
void ocall_refill_from_group(int group_idx, entry_t* buffer,
                             size_t buffer_size, size_t* actual_filled) {
    // TDX: Stub implementation - will be replaced by actual shuffle manager callback
    // For now, mark as exhausted
    *actual_filled = 0;
    (void)group_idx;
    (void)buffer;
    (void)buffer_size;
}

// Callback for k-way shuffle to flush output
void ocall_flush_output(entry_t* buffer, size_t buffer_size) {
    // TDX: Stub implementation - will be replaced by actual shuffle manager callback
    (void)buffer;
    (void)buffer_size;
}

} // extern "C"
