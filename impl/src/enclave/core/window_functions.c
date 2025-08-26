#include "../enclave_types.h"
#include <stdint.h>

/**
 * Window functions for oblivious processing
 * All functions are implemented with oblivious (branchless) operations
 * to prevent information leakage through memory access patterns
 */

/**
 * Set original index for window processing
 * Sets consecutive indices as the window slides through the table
 */
void window_set_original_index(entry_t* e1, entry_t* e2) {
    e2->original_index = e1->original_index + 1;
}

/**
 * Update target multiplicity (Algorithm 503)
 * Pure arithmetic - already oblivious
 */
void update_target_multiplicity(entry_t* target, const entry_t* source) {
    // Multiply target's local_mult by computed interval from combined table
    target->local_mult = target->local_mult * source->local_interval;
}

/**
 * Update target final multiplicity (Algorithm 623)
 * Pure arithmetic - already oblivious
 */
void update_target_final_multiplicity(entry_t* target, const entry_t* source) {
    // Propagate foreign intervals to compute final multiplicities
    target->final_mult = source->foreign_interval * target->local_mult;
    target->foreign_sum = source->foreign_sum;  // For alignment
}

/**
 * Window compute local sum (Algorithm 424)
 * Oblivious conversion: use arithmetic instead of branching
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_local_sum(entry_t* e1, entry_t* e2) {
    // Check if e2 (window[1]) is SOURCE type (obliviously)
    int32_t is_source = (e2->field_type == SOURCE);
    
    // Add local_mult only if SOURCE, otherwise add 0
    e2->local_cumsum = e1->local_cumsum + (is_source * e2->local_mult);
}

/**
 * Window compute local interval (Algorithm 467)
 * Oblivious conversion: use conditional arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_local_interval(entry_t* e1, entry_t* e2) {
    // Check if we have a START/END pair (obliviously)
    int32_t is_start = (e1->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_pair = is_start * is_end;
    
    // Compute interval difference
    int32_t interval = e2->local_cumsum - e1->local_cumsum;
    
    // Set interval only if we have a pair, otherwise preserve existing value
    e2->local_interval = (is_pair * interval) + ((1 - is_pair) * e2->local_interval);
}

/**
 * Window compute foreign sum (Algorithm 590)
 * Oblivious conversion: convert 3-way branch to arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_foreign_sum(entry_t* e1, entry_t* e2) {
    // Determine entry type (obliviously)
    int32_t is_start = (e2->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_source = (e2->field_type == SOURCE);
    
    // Calculate weight delta: START adds, END subtracts, SOURCE no change
    int32_t weight_delta = (is_start * e2->local_mult) - 
                          (is_end * e2->local_mult);
    
    // Update local weight
    e2->local_weight = e1->local_weight + weight_delta;
    
    // Calculate foreign delta for SOURCE entries
    // Use safe denominator to avoid division by zero
    int32_t safe_denom = is_source * e1->local_weight + (1 - is_source);
    int32_t foreign_delta = is_source * (e2->final_mult / safe_denom);
    
    // Update foreign cumulative sum
    e2->foreign_cumsum = e1->foreign_cumsum + foreign_delta;
}

/**
 * Window compute foreign interval (Algorithm 609)
 * Oblivious conversion: use conditional arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_foreign_interval(entry_t* e1, entry_t* e2) {
    // Check if we have a START/END pair (obliviously)
    int32_t is_start = (e1->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_pair = is_start * is_end;
    
    // Compute foreign interval difference
    int32_t interval = e2->foreign_cumsum - e1->foreign_cumsum;
    
    // Set interval and foreign_sum only if we have a pair
    e2->foreign_interval = (is_pair * interval) + 
                          ((1 - is_pair) * e2->foreign_interval);
    e2->foreign_sum = (is_pair * e1->foreign_cumsum) + 
                     ((1 - is_pair) * e2->foreign_sum);
}