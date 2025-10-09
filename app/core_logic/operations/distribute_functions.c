#include "enclave_types.h"
#include "debug_util.h"
#include <stdint.h>

/**
 * Distribute utility functions
 * This file contains utility functions for the distribute-expand phase
 * Transform and window functions have been moved to their respective files
 */

/**
 * Get output size from last entry
 * Returns dst_idx + final_mult
 */
int32_t obtain_output_size(const entry_t* last_entry) {
    // TDX: Direct access, no encryption to check
    return last_entry->dst_idx + last_entry->final_mult;
}