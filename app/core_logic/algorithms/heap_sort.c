#include "min_heap.h"
#include "enclave_types.h"
#include "batch_types.h"
#include <string.h>
#include <stdint.h>

// External function to get merge comparator
extern comparator_func_t get_merge_comparator(OpEcall type);

/**
 * Heap sort implementation for TDX
 * Sorts an array of entries in-place using heap sort
 *
 * TDX migration: No encryption/decryption needed - data is protected by VM-level encryption
 */
void heap_sort_entries(entry_t* array, size_t size, int comparator_type) {
    if (!array || size == 0) {
        return;  // Nothing to sort
    }

    // Get the appropriate comparator function
    comparator_func_t compare = get_merge_comparator((OpEcall)comparator_type);

    // Perform heap sort
    heap_sort(array, size, compare);
}
