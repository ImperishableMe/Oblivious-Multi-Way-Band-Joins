#ifndef ENCLAVE_CORE_H
#define ENCLAVE_CORE_H

#include "../enclave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transform functions for Map operations
 */
void transform_initialize_leaf(entry_t* entry);
void transform_add_metadata(entry_t* entry);
void transform_set_index(entry_t* entry, uint32_t index);
void transform_init_local_temps(entry_t* entry);
void transform_to_source(entry_t* entry);
void transform_to_start(entry_t* entry, int32_t deviation, equality_type_t equality);
void transform_to_end(entry_t* entry, int32_t deviation, equality_type_t equality);
void transform_set_empty(entry_t* entry);

/**
 * Window functions for oblivious processing
 */
void window_set_original_index(entry_t* e1, entry_t* e2);
void update_target_multiplicity(entry_t* target, const entry_t* source);
void update_target_final_multiplicity(entry_t* target, const entry_t* source);
void window_compute_local_sum(entry_t* e1, entry_t* e2);
void window_compute_local_interval(entry_t* e1, entry_t* e2);
void window_compute_foreign_sum(entry_t* e1, entry_t* e2);
void window_compute_foreign_interval(entry_t* e1, entry_t* e2);

/**
 * Comparator functions for oblivious sorting
 * These modify the entries in-place by swapping if needed
 */
void comparator_join_attr(entry_t* e1, entry_t* e2);
void comparator_pairwise(entry_t* e1, entry_t* e2);
void comparator_end_first(entry_t* e1, entry_t* e2);
void comparator_join_then_other(entry_t* e1, entry_t* e2);
void comparator_original_index(entry_t* e1, entry_t* e2);
void comparator_alignment_key(entry_t* e1, entry_t* e2);

/**
 * Oblivious swap utility
 */
void oblivious_swap(entry_t* e1, entry_t* e2, int should_swap);

#ifdef __cplusplus
}
#endif

#endif // ENCLAVE_CORE_H