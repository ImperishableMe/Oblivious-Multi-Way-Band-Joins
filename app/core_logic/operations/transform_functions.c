#include "enclave_types.h"
#include "debug_util.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

/**
 * Transform functions for Map operations
 * Using crypto helpers to handle decrypt-modify-encrypt pattern
 * Operations are oblivious (branchless where possible) to prevent leakage
 */

// Operation functions for transforms
void transform_set_local_mult_one_op(entry_t* entry) {
    entry->local_mult = 1;
    entry->final_mult = 0;  // Initialize to 0
}

/**
 * Set local_mult = 1 for all tables in bottom-up phase initialization
 */
void transform_set_local_mult_one(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_set_local_mult_one_op(entry);
}

void transform_add_metadata_op(entry_t* entry) {
    // Initialize persistent metadata
    entry->original_index = 0;
    entry->local_mult = 0;
    entry->final_mult = 0;
    entry->foreign_sum = 0;
    
    // Initialize temporary metadata
    entry->local_cumsum = 0;
    entry->local_interval = 0;
    entry->foreign_interval = 0;
    entry->local_weight = 0;
    
    // Initialize expansion metadata
    entry->copy_index = 0;
    entry->alignment_key = 0;
}

/**
 * Add metadata columns with null/zero placeholders
 * Initializes all metadata fields to prepare for algorithm phases
 */
void transform_add_metadata(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_add_metadata_op(entry);
}

void transform_set_index_op(entry_t* entry, uint32_t index) {
    entry->original_index = (int32_t)index;
}

/**
 * Set original index for an entry
 * Used during initialization to assign sequential indices
 */
void transform_set_index(entry_t* entry, uint32_t index) {
    // TDX: Direct call, no encryption wrapper needed
    transform_set_index_op(entry, index);
}

void transform_init_local_temps_op(entry_t* entry) {
    // Initialize for bottom-up phase
    entry->local_cumsum = entry->local_mult;
    entry->local_interval = 0;
}

/**
 * Initialize temporary fields for bottom-up computation
 * Sets local_cumsum = local_mult, local_interval = 0
 */
void transform_init_local_temps(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_init_local_temps_op(entry);
}

void transform_to_source_op(entry_t* entry) {
    // Set type to SOURCE
    entry->field_type = SOURCE;
    entry->equality_type = NONE;  // SOURCE entries have no equality type
}

/**
 * Transform entry to SOURCE type
 * Used when creating combined table from source (child) entries
 */
void transform_to_source(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_to_source_op(entry);
}

void transform_to_start_op(entry_t* entry, int32_t deviation, equality_type_t equality) {
    // Transform to START boundary
    entry->field_type = START;
    entry->equality_type = equality;
    
    // Debug: Check if values are within expected range
    int32_t original_join_attr __attribute__((unused)) = entry->join_attr;
    int32_t new_join_attr = entry->join_attr + deviation;
    
    // Check if original value is within design constraint
    if (entry->join_attr < INT32_MIN/2 || entry->join_attr > INT32_MAX/2) {
        DEBUG_WARN("to_start_op: join_attr=%d is outside design range [%d, %d]", 
                   entry->join_attr, INT32_MIN/2, INT32_MAX/2);
    }
    
    entry->join_attr = new_join_attr;
    
    DEBUG_TRACE("to_start_op: original=%d, deviation=%d, new=%d", 
                original_join_attr, deviation, new_join_attr);
    
    // Preserve final_mult if already set (for top-down phase)
    // final_mult remains unchanged
}

/**
 * Transform entry to START boundary
 * Creates the start of a matching range for band joins
 */
void transform_to_start(entry_t* entry, int32_t deviation, equality_type_t equality) {
    // TDX: Direct call, no encryption wrapper needed
    transform_to_start_op(entry, deviation, equality);
}

void transform_to_end_op(entry_t* entry, int32_t deviation, equality_type_t equality) {
    // Transform to END boundary
    entry->field_type = END;
    entry->equality_type = equality;
    
    // Debug: Check if values are within expected range
    int32_t original_join_attr __attribute__((unused)) = entry->join_attr;
    int32_t new_join_attr = entry->join_attr + deviation;
    
    // Check for overflow/underflow
    if (deviation > 0 && entry->join_attr > INT32_MAX - deviation) {
        DEBUG_WARN("to_end_op overflow detected! join_attr=%d + deviation=%d would overflow", 
                   entry->join_attr, deviation);
    }
    
    // Check if original value is within design constraint
    if (entry->join_attr < INT32_MIN/2 || entry->join_attr > INT32_MAX/2) {
        DEBUG_WARN("to_end_op: join_attr=%d is outside design range [%d, %d]", 
                   entry->join_attr, INT32_MIN/2, INT32_MAX/2);
    }
    
    entry->join_attr = new_join_attr;
    
    DEBUG_TRACE("to_end_op: original=%d, deviation=%d, new=%d", 
                original_join_attr, deviation, new_join_attr);
    
    // Preserve final_mult if already set (for top-down phase)
    // final_mult remains unchanged
}

/**
 * Transform entry to END boundary
 * Creates the end of a matching range for band joins
 */
void transform_to_end(entry_t* entry, int32_t deviation, equality_type_t equality) {
    // TDX: Direct call, no encryption wrapper needed
    transform_to_end_op(entry, deviation, equality);
}

/**
 * Transform entry to SORT_PADDING type (for bitonic sort padding)
 */
void transform_set_sort_padding_op(entry_t* entry) {
    entry->field_type = SORT_PADDING;
    entry->join_attr = INT32_MAX;  // Sort to end
    entry->original_index = NULL_VALUE;  // Use NULL_VALUE to indicate padding
    entry->local_mult = 0;
    entry->final_mult = 0;
    // Clear other fields
    entry->foreign_sum = 0;
    entry->local_cumsum = 0;
    entry->local_interval = 0;
    entry->foreign_interval = 0;
    entry->local_weight = 0;
}

void transform_set_sort_padding(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_set_sort_padding_op(entry);
}

/**
 * Initialize final_mult from local_mult (for root table in top-down)
 */
void transform_init_final_mult_op(entry_t* entry) {
    entry->final_mult = entry->local_mult;
    // Also initialize foreign fields
    entry->foreign_sum = 0;
    entry->foreign_interval = 0;
    entry->local_weight = 0;
}

void transform_init_final_mult(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_init_final_mult_op(entry);
}

/**
 * Initialize foreign temporary fields for top-down computation
 */
void transform_init_foreign_temps_op(entry_t* entry) {
    // Initialize foreign tracking fields
    entry->foreign_sum = 0;
    entry->foreign_interval = 0;
    entry->local_weight = entry->local_mult;  // Initialize to local_mult per pseudocode
    // Preserve final_mult from parent if it's a START/END entry
    // For SOURCE entries, final_mult will be computed later
}

void transform_init_foreign_temps(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_init_foreign_temps_op(entry);
}

// ============================================================================
// Distribute-Expand Transform Functions
// ============================================================================

/**
 * Initialize destination index to 0
 */
void transform_init_dst_idx_op(entry_t* entry) {
    entry->dst_idx = 0;
}

void transform_init_dst_idx(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_init_dst_idx_op(entry);
}

/**
 * Initialize index field to 0
 */
void transform_init_index_op(entry_t* entry) {
    entry->index = 0;
}

void transform_init_index(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_init_index_op(entry);
}

/**
 * Mark entries with final_mult = 0 as DIST_PADDING
 */
void transform_mark_zero_mult_padding_op(entry_t* entry) {
    // Use oblivious selection to avoid branching
    int is_zero = (entry->final_mult == 0);
    entry->field_type = is_zero * DIST_PADDING + (1 - is_zero) * entry->field_type;
}

void transform_mark_zero_mult_padding(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_mark_zero_mult_padding_op(entry);
}

/**
 * Create a distribution padding entry
 */
void transform_create_dist_padding_op(entry_t* entry) {
    // Initialize a padding entry
    entry->field_type = DIST_PADDING;
    entry->final_mult = 0;
    entry->dst_idx = -1;
    entry->index = 0;
    entry->original_index = -1;
    entry->local_mult = 0;
    // Other fields can remain as is or be zeroed
}

void transform_create_dist_padding(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_create_dist_padding_op(entry);
}

// ============================================================================
// Align-Concat Transform Functions
// ============================================================================

/**
 * Initialize copy index to 0
 */
void transform_init_copy_index_op(entry_t* entry) {
    entry->copy_index = 0;
}

void transform_init_copy_index(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_init_copy_index_op(entry);
}

/**
 * Compute alignment key = foreign_sum + (copy_index / local_mult)
 */
void transform_compute_alignment_key_op(entry_t* entry) {
    // Avoid division by zero obliviously
    int32_t safe_local_mult = entry->local_mult + (entry->local_mult == 0);
    entry->alignment_key = entry->foreign_sum + (entry->copy_index / safe_local_mult);
}

void transform_compute_alignment_key(entry_t* entry) {
    // TDX: Direct call, no encryption wrapper needed
    transform_compute_alignment_key_op(entry);
}

/**
 * Compute alignment key = foreign_sum + (rank mod groups), where rank is the
 * row's rank among its identical rows (in copy_index, see
 * window_update_duplicate_rank_op) and groups = final_mult / local_mult is the
 * number of parent groups the tuple is spread over.
 *
 * Every distinct sub-result of a tuple occurs a multiple of `groups` times, so
 * rank mod groups hands each parent group exactly its share of every
 * sub-result, whatever order the rows were in.
 */
void transform_compute_rank_alignment_key_op(entry_t* entry) {
    // Avoid division by zero obliviously
    int32_t safe_local_mult = entry->local_mult + (entry->local_mult == 0);
    int32_t groups = entry->final_mult / safe_local_mult;
    int32_t safe_groups = groups + (groups == 0);
    entry->alignment_key = entry->foreign_sum + (entry->copy_index % safe_groups);
}

/**
 * Set the composite join key from the entry's own columns (Algorithm 999,
 * extended to several join columns).
 *
 * key_params uses the JOIN_KEY_PARAM_* layout from constants.h:
 *   - the range column is copied into join_attr;
 *   - the first num_eq equality columns, each plus its constant offset, are
 *     copied into join_eq; the remaining join_eq slots are set to 0 so they
 *     never influence the key order.
 *
 * Which columns form the key, and how many, is fixed by the query, so the
 * branches on key_params are public; nothing here branches on row values.
 * The caller validates every column index against the table schema.
 */
void transform_set_join_key_op(entry_t* entry, const int32_t* key_params) {
    const int32_t range_column = key_params[JOIN_KEY_PARAM_RANGE_COLUMN];
    const int32_t num_eq = key_params[JOIN_KEY_PARAM_NUM_EQ];

    entry->join_attr = entry->attributes[range_column];

    // Debug: Check if the join_attr is within design constraints
    if (entry->join_attr < INT32_MIN/2 || entry->join_attr > INT32_MAX/2) {
        DEBUG_WARN("set_join_key: value %d from attributes[%d] is outside design range [%d, %d]",
                   entry->join_attr, range_column, INT32_MIN/2, INT32_MAX/2);
    }
    DEBUG_TRACE("set_join_key: range_column=%d, value=%d", range_column, entry->join_attr);

    for (int32_t i = 0; i < MAX_JOIN_EQ; i++) {
        int32_t value = 0;
        if (i < num_eq) {
            value = entry->attributes[key_params[JOIN_KEY_PARAM_EQ_COLUMNS + i]]
                  + key_params[JOIN_KEY_PARAM_EQ_OFFSETS + i];
        }
        entry->join_eq[i] = value;
    }
}

/**
 * Initialize metadata fields to NULL_VALUE based on field mask
 * This provides fine-grained control over which fields to initialize
 */
void transform_init_metadata_null_op(entry_t* entry, uint32_t field_mask) {
    // Check each individual mask bit and initialize corresponding fields
    // This allows any subset of fields to be set to NULL_VALUE
    
    // Persistent metadata fields
    if (field_mask & METADATA_ORIGINAL_INDEX) {
        entry->original_index = NULL_VALUE;
    }
    if (field_mask & METADATA_LOCAL_MULT) {
        entry->local_mult = NULL_VALUE;
    }
    if (field_mask & METADATA_FINAL_MULT) {
        entry->final_mult = NULL_VALUE;
    }
    if (field_mask & METADATA_FOREIGN_SUM) {
        entry->foreign_sum = NULL_VALUE;
    }
    
    // Temporary metadata fields
    if (field_mask & METADATA_LOCAL_CUMSUM) {
        entry->local_cumsum = NULL_VALUE;
    }
    if (field_mask & METADATA_LOCAL_INTERVAL) {
        entry->local_interval = NULL_VALUE;
    }
    if (field_mask & METADATA_FOREIGN_INTERVAL) {
        entry->foreign_interval = NULL_VALUE;
    }
    if (field_mask & METADATA_LOCAL_WEIGHT) {
        entry->local_weight = NULL_VALUE;
    }
    
    // Distribution metadata fields
    if (field_mask & METADATA_DST_IDX) {
        entry->dst_idx = NULL_VALUE;
    }
    if (field_mask & METADATA_INDEX) {
        entry->index = NULL_VALUE;
    }
    
    // Alignment metadata fields
    if (field_mask & METADATA_COPY_INDEX) {
        entry->copy_index = NULL_VALUE;
    }
    if (field_mask & METADATA_ALIGNMENT_KEY) {
        entry->alignment_key = NULL_VALUE;
    }
    
    // Type field - set to NULL_VALUE like other metadata
    if (field_mask & METADATA_FIELD_TYPE) {
        // Set to NULL_VALUE for clarity in debugging
        // The algorithm will explicitly set these when needed
        entry->field_type = NULL_VALUE;
        entry->equality_type = NULL_VALUE;
    }
}

void transform_init_metadata_null(entry_t* entry, uint32_t field_mask) {
    // TDX: Direct call, no encryption wrapper needed
    transform_init_metadata_null_op(entry, field_mask);
}