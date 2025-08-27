#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "crypto_helpers.h"
#include "../../common/debug_util.h"
#include <stdint.h>

/**
 * Window functions for oblivious processing
 * Using crypto helpers to handle decrypt-modify-encrypt pattern
 * All functions are implemented with oblivious (branchless) operations
 * to prevent information leakage through memory access patterns
 */

// Operation functions for window operations
static void set_original_index_op(entry_t* e1, entry_t* e2) {
    e2->original_index = e1->original_index + 1;
}

/**
 * Set original index for window processing
 * Sets consecutive indices as the window slides through the table
 */
void window_set_original_index(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, set_original_index_op);
}

static void update_target_multiplicity_op(entry_t* target, entry_t* source) {
    // Multiply target's local_mult by computed interval from combined table
    target->local_mult = target->local_mult * source->local_interval;
}

/**
 * Update target multiplicity (Algorithm 503)
 * Pure arithmetic - already oblivious
 */
void update_target_multiplicity(entry_t* target, entry_t* source) {
    apply_to_decrypted_pair(target, source, update_target_multiplicity_op);
}

static void update_target_final_multiplicity_op(entry_t* target, entry_t* source) {
    // Propagate foreign intervals to compute final multiplicities
    target->final_mult = source->foreign_interval * target->local_mult;
    target->foreign_sum = source->foreign_sum;  // For alignment
}

/**
 * Update target final multiplicity (Algorithm 623)
 * Pure arithmetic - already oblivious
 */
void update_target_final_multiplicity(entry_t* target, entry_t* source) {
    apply_to_decrypted_pair(target, source, update_target_final_multiplicity_op);
}

static void compute_local_sum_op(entry_t* e1, entry_t* e2) {
    // Check if e2 (window[1]) is SOURCE type (obliviously)
    int32_t is_source = (e2->field_type == SOURCE);
    
    // Add local_mult only if SOURCE, otherwise add 0
    uint32_t old_cumsum = e2->local_cumsum;
    e2->local_cumsum = e1->local_cumsum + (is_source * e2->local_mult);
    
    // Debug log to verify the operation
    DEBUG_DEBUG("compute_local_sum: e1(type=%d,cumsum=%u) e2(type=%d,mult=%u) is_source=%d result=%u->%u",
        e1->field_type, e1->local_cumsum, 
        e2->field_type, e2->local_mult, 
        is_source, old_cumsum, e2->local_cumsum);
}

/**
 * Window compute local sum (Algorithm 424)
 * Oblivious conversion: use arithmetic instead of branching
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_local_sum(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, compute_local_sum_op);
}

static void compute_local_interval_op(entry_t* e1, entry_t* e2) {
    // Check if we have a START/END pair (obliviously)
    int32_t is_start = (e1->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_pair = is_start * is_end;
    
    // Compute interval difference
    int32_t interval = e2->local_cumsum - e1->local_cumsum;
    uint32_t old_interval = e2->local_interval;
    
    // Set interval only if we have a pair, otherwise preserve existing value
    e2->local_interval = (is_pair * interval) + ((1 - is_pair) * e2->local_interval);
    
    // Debug log
    DEBUG_DEBUG("compute_interval: e1(type=%d,cumsum=%u) e2(type=%d,cumsum=%u) is_pair=%d interval=%d result=%u->%u",
        e1->field_type, e1->local_cumsum,
        e2->field_type, e2->local_cumsum,
        is_pair, interval, old_interval, e2->local_interval);
}

/**
 * Window compute local interval (Algorithm 467)
 * Oblivious conversion: use conditional arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_local_interval(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, compute_local_interval_op);
}

static void compute_foreign_sum_op(entry_t* e1, entry_t* e2) {
    // Determine entry type (obliviously)
    int32_t is_start = (e2->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_source = (e2->field_type == SOURCE);
    
    // For START/END entries from child, use local_mult
    // For SOURCE entries from parent, use final_mult
    int32_t mult_to_use = is_source ? e2->final_mult : e2->local_mult;
    
    // Calculate weight delta: START adds, END subtracts, SOURCE no change
    int32_t weight_delta = (is_start * e2->local_mult) - 
                          (is_end * e2->local_mult);
    
    // Update local weight
    e2->local_weight = e1->local_weight + weight_delta;
    
    // Calculate foreign delta for SOURCE entries
    // SOURCE entries (parent) contribute final_mult / local_weight
    // Avoid division by zero
    int32_t safe_weight = e2->local_weight ? e2->local_weight : 1;
    int32_t foreign_delta = is_source * (e2->final_mult / safe_weight);
    
    // Update foreign cumulative sum
    e2->foreign_cumsum = e1->foreign_cumsum + foreign_delta;
}

/**
 * Window compute foreign sum (Algorithm 590)
 * Oblivious conversion: convert 3-way branch to arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_foreign_sum(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, compute_foreign_sum_op);
}

static void compute_foreign_interval_op(entry_t* e1, entry_t* e2) {
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

/**
 * Window compute foreign interval (Algorithm 609)
 * Oblivious conversion: use conditional arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_foreign_interval(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, compute_foreign_interval_op);
}

static void propagate_foreign_interval_op(entry_t* e1, entry_t* e2) {
    // Check entry types
    int32_t is_source2 = (e2->field_type == SOURCE);
    int32_t is_end1 = (e1->field_type == END);
    
    // If e1 is END and e2 is SOURCE, propagate the foreign_interval
    // Otherwise keep e2's existing values
    int32_t should_propagate = is_end1 * is_source2;
    
    e2->foreign_interval = (should_propagate * e1->foreign_interval) + 
                          ((1 - should_propagate) * e2->foreign_interval);
    e2->foreign_sum = (should_propagate * e1->foreign_sum) + 
                     ((1 - should_propagate) * e2->foreign_sum);
}

/**
 * Propagate foreign interval from END entries to SOURCE entries
 * After computing intervals for START/END pairs, propagate to SOURCE entries
 */
void window_propagate_foreign_interval(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, propagate_foreign_interval_op);
}

// ============================================================================
// Distribute-Expand Window Functions
// ============================================================================

/**
 * Compute destination index as cumulative sum of final_mult
 */
static void compute_dst_idx_op(entry_t* e1, entry_t* e2) {
    // e2's dst_idx = e1's dst_idx + e1's final_mult
    e2->dst_idx = e1->dst_idx + e1->final_mult;
}

void window_compute_dst_idx(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, compute_dst_idx_op);
}

/**
 * Set sequential index values
 */
static void increment_index_op(entry_t* e1, entry_t* e2) {
    // e2's index = e1's index + 1
    e2->index = e1->index + 1;
}

void window_increment_index(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, increment_index_op);
}

/**
 * Expansion: copy non-empty entries to fill padding slots
 */
static void expand_copy_op(entry_t* e1, entry_t* e2) {
    // Check if e2 is DIST_PADDING (obliviously)
    int is_padding = (e2->field_type == DIST_PADDING);
    
    // Save e2's index (always done to maintain oblivious pattern)
    int32_t saved_index = e2->index;
    
    // Conditionally copy fields from e1 to e2
    // If is_padding=1, copy from e1; if is_padding=0, keep e2
    e2->field_type = is_padding * e1->field_type + (1 - is_padding) * e2->field_type;
    e2->equality_type = is_padding * e1->equality_type + (1 - is_padding) * e2->equality_type;
    e2->join_attr = is_padding * e1->join_attr + (1 - is_padding) * e2->join_attr;
    e2->original_index = is_padding * e1->original_index + (1 - is_padding) * e2->original_index;
    e2->local_mult = is_padding * e1->local_mult + (1 - is_padding) * e2->local_mult;
    e2->final_mult = is_padding * e1->final_mult + (1 - is_padding) * e2->final_mult;
    e2->foreign_sum = is_padding * e1->foreign_sum + (1 - is_padding) * e2->foreign_sum;
    e2->dst_idx = is_padding * e1->dst_idx + (1 - is_padding) * e2->dst_idx;
    
    // Copy attributes obliviously
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        e2->attributes[i] = is_padding * e1->attributes[i] + (1 - is_padding) * e2->attributes[i];
    }
    
    // Copy column names obliviously
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        for (int j = 0; j < MAX_COLUMN_NAME_LEN; j++) {
            e2->column_names[i][j] = is_padding * e1->column_names[i][j] + 
                                     (1 - is_padding) * e2->column_names[i][j];
        }
    }
    
    // Restore e2's index
    e2->index = saved_index;
    
    // Update copy_index to track which copy this is
    int not_padding_e1 = (e1->field_type != DIST_PADDING);
    e2->copy_index = is_padding * not_padding_e1 * (e2->index - e1->dst_idx) + 
                     (1 - (is_padding * not_padding_e1)) * e2->copy_index;
}

void window_expand_copy(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, expand_copy_op);
}

// ============================================================================
// Align-Concat Window Functions
// ============================================================================

/**
 * Update copy index based on original index
 * If same original index, increment from previous
 * If different, reset to 0
 */
static void update_copy_index_op(entry_t* e1, entry_t* e2) {
    // Check if same original tuple (obliviously)
    int is_same = (e1->original_index == e2->original_index);
    
    // If same, increment; if different, reset to 0
    e2->copy_index = is_same * (e1->copy_index + 1) + (1 - is_same) * 0;
}

void window_update_copy_index(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, update_copy_index_op);
}