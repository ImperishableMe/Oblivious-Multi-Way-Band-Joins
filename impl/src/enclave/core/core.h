#ifndef ENCLAVE_CORE_H
#define ENCLAVE_CORE_H

#include "../enclave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transform functions for Map operations
 */
void transform_set_local_mult_one(entry_t* entry);
void transform_add_metadata(entry_t* entry);
void transform_set_index(entry_t* entry, uint32_t index);
void transform_init_local_temps(entry_t* entry);
void transform_init_final_mult(entry_t* entry);
void transform_init_foreign_temps(entry_t* entry);
void transform_to_source(entry_t* entry);
void transform_to_start(entry_t* entry, int32_t deviation, equality_type_t equality);
void transform_to_end(entry_t* entry, int32_t deviation, equality_type_t equality);
void transform_set_sort_padding(entry_t* entry);

/**
 * Distribute-expand transform functions
 */
void transform_init_dst_idx(entry_t* entry);
void transform_init_index(entry_t* entry);
void transform_mark_zero_mult_padding(entry_t* entry);
void transform_create_dist_padding(entry_t* entry);

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
void window_propagate_foreign_interval(entry_t* e1, entry_t* e2);

/**
 * Distribute-expand window functions
 */
void window_compute_dst_idx(entry_t* e1, entry_t* e2);
void window_increment_index(entry_t* e1, entry_t* e2);
void window_expand_copy(entry_t* e1, entry_t* e2);

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
void comparator_padding_last(entry_t* e1, entry_t* e2);
void comparator_distribute(entry_t* e1, entry_t* e2);

/**
 * Oblivious swap utility
 */
void oblivious_swap(entry_t* e1, entry_t* e2, int should_swap);

/**
 * Utility functions
 */
int32_t obtain_output_size(const entry_t* last_entry);

/**
 * Align-Concat phase functions
 */
void transform_init_copy_index(entry_t* entry);
void transform_compute_alignment_key(entry_t* entry);
void window_update_copy_index(entry_t* e1, entry_t* e2);
void concat_attributes(entry_t* left, entry_t* right);

/**
 * Join attribute setting
 */
void transform_set_join_attr(entry_t* entry, int32_t column_index);

#ifdef __cplusplus
}
#endif

#endif // ENCLAVE_CORE_H