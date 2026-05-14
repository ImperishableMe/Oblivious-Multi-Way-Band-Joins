#include "min_heap.h"
#include <stdlib.h>
#include <string.h>

// Helper functions for heap operations
static inline size_t parent(size_t i) { return (i - 1) / 2; }
static inline size_t left_child(size_t i) { return 2 * i + 1; }
static inline size_t right_child(size_t i) { return 2 * i + 2; }

// Swap two heap elements
static void heap_swap(MinHeap* heap, size_t i, size_t j) {
    // Swap entries
    entry_t temp_entry = heap->entries[i];
    heap->entries[i] = heap->entries[j];
    heap->entries[j] = temp_entry;
    
    // Swap run indices
    size_t temp_idx = heap->run_indices[i];
    heap->run_indices[i] = heap->run_indices[j];
    heap->run_indices[j] = temp_idx;
}

// Heapify up (for insertion)
static void heapify_up(MinHeap* heap, size_t i) {
    while (i > 0) {
        size_t p = parent(i);
        // If parent is greater than child, swap
        if (heap->compare(&heap->entries[p], &heap->entries[i]) == 0 &&
            heap->compare(&heap->entries[i], &heap->entries[p]) == 1) {
            heap_swap(heap, i, p);
            i = p;
        } else {
            break;
        }
    }
}

// Heapify down (for deletion)
static void heapify_down(MinHeap* heap, size_t i) {
    while (1) {
        size_t smallest = i;
        size_t left = left_child(i);
        size_t right = right_child(i);
        
        // Find smallest among node and its children
        if (left < heap->size && 
            heap->compare(&heap->entries[left], &heap->entries[smallest]) == 1) {
            smallest = left;
        }
        
        if (right < heap->size && 
            heap->compare(&heap->entries[right], &heap->entries[smallest]) == 1) {
            smallest = right;
        }
        
        if (smallest != i) {
            heap_swap(heap, i, smallest);
            i = smallest;
        } else {
            break;
        }
    }
}

// Initialize heap
void minheap_init(MinHeap* heap, size_t capacity, comparator_func_t compare) {
    heap->entries = (entry_t*)malloc(capacity * sizeof(entry_t));
    heap->run_indices = (size_t*)malloc(capacity * sizeof(size_t));
    heap->size = 0;
    heap->capacity = capacity;
    heap->compare = compare;
}

// Push entry to heap
void heap_push(MinHeap* heap, entry_t* entry, size_t run_idx) {
    if (heap->size >= heap->capacity) {
        // Heap is full - this shouldn't happen in our use case
        return;
    }
    
    // Add to end of heap
    heap->entries[heap->size] = *entry;
    heap->run_indices[heap->size] = run_idx;
    
    // Heapify up
    heapify_up(heap, heap->size);
    heap->size++;
}

// Pop minimum from heap
int heap_pop(MinHeap* heap, entry_t* out_entry, size_t* out_run_idx) {
    if (heap->size == 0) {
        return 0;  // Heap is empty
    }
    
    // Return minimum (root)
    *out_entry = heap->entries[0];
    *out_run_idx = heap->run_indices[0];
    
    // Move last element to root
    heap->size--;
    if (heap->size > 0) {
        heap->entries[0] = heap->entries[heap->size];
        heap->run_indices[0] = heap->run_indices[heap->size];
        
        // Heapify down
        heapify_down(heap, 0);
    }
    
    return 1;  // Success
}

// Peek at minimum without removing
int heap_peek(MinHeap* heap, entry_t* out_entry, size_t* out_run_idx) {
    if (heap->size == 0) {
        return 0;  // Heap is empty
    }
    
    *out_entry = heap->entries[0];
    *out_run_idx = heap->run_indices[0];
    return 1;  // Success
}

// Check if heap is empty
int heap_is_empty(MinHeap* heap) {
    return heap->size == 0;
}

// Destroy heap
void heap_destroy(MinHeap* heap) {
    if (heap->entries) {
        free(heap->entries);
        heap->entries = NULL;
    }
    if (heap->run_indices) {
        free(heap->run_indices);
        heap->run_indices = NULL;
    }
    heap->size = 0;
    heap->capacity = 0;
}

// Heap sort implementation
//
// Indirect heap-sort: the heap is built over a permutation index array
// (one uint32_t per entry), not over the entry_t array itself. All swaps
// during heapify and extraction move 4 bytes instead of sizeof(entry_t)
// (~316 bytes). The original array is rearranged once at the end via a
// single in-place cycle-following permutation, doing N entry moves total
// instead of ~2 * N * log N entry moves under the naive variant.
//
// Obliviousness note: the pre-shuffle ensures inputs to this sort are
// uniformly random, so the comparator-driven control flow is data-trace-
// independent given public N. Indirection doesn't change that.
void heap_sort(entry_t* array, size_t size, comparator_func_t compare) {
    if (size <= 1) return;

    // We pack one "visited" bit into the high bit of each uint32_t index,
    // so source indices must fit in 31 bits.
    if (size > 0x7FFFFFFFull) return;

    uint32_t* idx = (uint32_t*)malloc(size * sizeof(uint32_t));
    if (!idx) return;
    for (size_t i = 0; i < size; i++) idx[i] = (uint32_t)i;

    // Build max-heap over indices.
    for (int i = (int)(size / 2) - 1; i >= 0; i--) {
        size_t cur = (size_t)i;
        while (1) {
            size_t largest = cur;
            size_t left = 2 * cur + 1;
            size_t right = 2 * cur + 2;

            if (left < size &&
                compare(&array[idx[largest]], &array[idx[left]]) == 1) {
                largest = left;
            }
            if (right < size &&
                compare(&array[idx[largest]], &array[idx[right]]) == 1) {
                largest = right;
            }

            if (largest != cur) {
                uint32_t tmp = idx[cur];
                idx[cur] = idx[largest];
                idx[largest] = tmp;
                cur = largest;
            } else {
                break;
            }
        }
    }

    // Extract elements one by one.
    for (size_t end = size - 1; end > 0; end--) {
        uint32_t tmp_root = idx[0];
        idx[0] = idx[end];
        idx[end] = tmp_root;

        size_t cur = 0;
        size_t heap_size = end;
        while (1) {
            size_t largest = cur;
            size_t left = 2 * cur + 1;
            size_t right = 2 * cur + 2;

            if (left < heap_size &&
                compare(&array[idx[largest]], &array[idx[left]]) == 1) {
                largest = left;
            }
            if (right < heap_size &&
                compare(&array[idx[largest]], &array[idx[right]]) == 1) {
                largest = right;
            }

            if (largest != cur) {
                uint32_t tmp = idx[cur];
                idx[cur] = idx[largest];
                idx[largest] = tmp;
                cur = largest;
            } else {
                break;
            }
        }
    }

    // idx[i] is the source slot for sorted position i. Apply the
    // permutation in place by following cycles. Total entry moves: N
    // (each entry written exactly once into its final slot).
    //
    // We use the high bit of idx[] as a "visited" marker. The size > 2^31
    // guard above ensures source indices fit in 31 bits.
    const uint32_t VISITED = 0x80000000u;
    entry_t scratch;
    for (size_t i = 0; i < size; i++) {
        if (idx[i] & VISITED) continue;
        if (idx[i] == (uint32_t)i) {
            idx[i] |= VISITED;
            continue;
        }

        // Walk the cycle starting at i: pull source value into scratch,
        // then shift everything along the cycle by one slot.
        size_t cur = i;
        scratch = array[i];
        while (1) {
            uint32_t src = idx[cur];
            idx[cur] = src | VISITED;
            if ((size_t)src == i) {
                // End of cycle: drop scratch into the last slot.
                array[cur] = scratch;
                break;
            }
            array[cur] = array[src];
            cur = src;
        }
    }

    free(idx);
}