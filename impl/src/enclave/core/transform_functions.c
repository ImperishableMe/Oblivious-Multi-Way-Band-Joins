#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "crypto_helpers.h"
#include "../../common/debug_util.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

/**
 * Transform functions for Map operations
 * Using crypto helpers to handle decrypt-modify-encrypt pattern
 * Operations are oblivious (branchless where possible) to prevent leakage
 */

// Static variables for parameter passing to operation functions
static int32_t g_deviation;
static equality_type_t g_equality;
static uint32_t g_index;

// Operation functions for transforms
static void set_local_mult_one_op(entry_t* entry) {
    entry->local_mult = 1;
    entry->final_mult = 0;  // Initialize to 0
}

/**
 * Set local_mult = 1 for all tables in bottom-up phase initialization
 */
void transform_set_local_mult_one(entry_t* entry) {
    apply_to_decrypted_entry(entry, set_local_mult_one_op);
}

static void add_metadata_op(entry_t* entry) {
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
    apply_to_decrypted_entry(entry, add_metadata_op);
}

static void set_index_op(entry_t* entry) {
    entry->original_index = g_index;
}

/**
 * Set original index for an entry
 * Used during initialization to assign sequential indices
 */
void transform_set_index(entry_t* entry, uint32_t index) {
    g_index = index;
    apply_to_decrypted_entry(entry, set_index_op);
}

static void init_local_temps_op(entry_t* entry) {
    // Initialize for bottom-up phase
    entry->local_cumsum = entry->local_mult;
    entry->local_interval = 0;
}

/**
 * Initialize temporary fields for bottom-up computation
 * Sets local_cumsum = local_mult, local_interval = 0
 */
void transform_init_local_temps(entry_t* entry) {
    apply_to_decrypted_entry(entry, init_local_temps_op);
}

static void to_source_op(entry_t* entry) {
    // Set type to SOURCE
    entry->field_type = SOURCE;
    entry->equality_type = NONE;  // SOURCE entries have no equality type
}

/**
 * Transform entry to SOURCE type
 * Used when creating combined table from source (child) entries
 */
void transform_to_source(entry_t* entry) {
    apply_to_decrypted_entry(entry, to_source_op);
}

static void to_start_op(entry_t* entry) {
    // Transform to START boundary
    entry->field_type = START;
    entry->equality_type = g_equality;
    
    // Debug: Check if values are within expected range
    int32_t original_join_attr = entry->join_attr;
    int32_t new_join_attr = entry->join_attr + g_deviation;
    
    // Check if original value is within design constraint
    if (entry->join_attr < INT32_MIN/2 || entry->join_attr > INT32_MAX/2) {
        DEBUG_WARN("to_start_op: join_attr=%d is outside design range [%d, %d]", 
                   entry->join_attr, INT32_MIN/2, INT32_MAX/2);
    }
    
    entry->join_attr = new_join_attr;
    
    DEBUG_TRACE("to_start_op: original=%d, deviation=%d, new=%d", 
                original_join_attr, g_deviation, new_join_attr);
    
    // Preserve final_mult if already set (for top-down phase)
    // final_mult remains unchanged
}

/**
 * Transform entry to START boundary
 * Creates the start of a matching range for band joins
 */
void transform_to_start(entry_t* entry, int32_t deviation, equality_type_t equality) {
    g_deviation = deviation;
    g_equality = equality;
    apply_to_decrypted_entry(entry, to_start_op);
}

static void to_end_op(entry_t* entry) {
    // Transform to END boundary
    entry->field_type = END;
    entry->equality_type = g_equality;
    
    // Debug: Check if values are within expected range
    int32_t original_join_attr = entry->join_attr;
    int32_t new_join_attr = entry->join_attr + g_deviation;
    
    // Check for overflow/underflow
    if (g_deviation > 0 && entry->join_attr > INT32_MAX - g_deviation) {
        DEBUG_WARN("to_end_op overflow detected! join_attr=%d + deviation=%d would overflow", 
                   entry->join_attr, g_deviation);
    }
    
    // Check if original value is within design constraint
    if (entry->join_attr < INT32_MIN/2 || entry->join_attr > INT32_MAX/2) {
        DEBUG_WARN("to_end_op: join_attr=%d is outside design range [%d, %d]", 
                   entry->join_attr, INT32_MIN/2, INT32_MAX/2);
    }
    
    entry->join_attr = new_join_attr;
    
    DEBUG_TRACE("to_end_op: original=%d, deviation=%d, new=%d", 
                original_join_attr, g_deviation, new_join_attr);
    
    // Preserve final_mult if already set (for top-down phase)
    // final_mult remains unchanged
}

/**
 * Transform entry to END boundary
 * Creates the end of a matching range for band joins
 */
void transform_to_end(entry_t* entry, int32_t deviation, equality_type_t equality) {
    g_deviation = deviation;
    g_equality = equality;
    apply_to_decrypted_entry(entry, to_end_op);
}

/**
 * Transform entry to SORT_PADDING type (for bitonic sort padding)
 */
static void set_sort_padding_op(entry_t* entry) {
    entry->field_type = SORT_PADDING;
    entry->join_attr = INT32_MAX;  // Sort to end
    entry->original_index = UINT32_MAX;
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
    apply_to_decrypted_entry(entry, set_sort_padding_op);
}

/**
 * Initialize final_mult from local_mult (for root table in top-down)
 */
static void init_final_mult_op(entry_t* entry) {
    entry->final_mult = entry->local_mult;
    // Also initialize foreign fields
    entry->foreign_sum = 0;
    entry->foreign_interval = 0;
    entry->local_weight = 0;
}

void transform_init_final_mult(entry_t* entry) {
    apply_to_decrypted_entry(entry, init_final_mult_op);
}

/**
 * Initialize foreign temporary fields for top-down computation
 */
static void init_foreign_temps_op(entry_t* entry) {
    // Initialize foreign tracking fields
    entry->foreign_sum = 0;
    entry->foreign_interval = 0;
    entry->local_weight = entry->local_mult;  // Initialize to local_mult per pseudocode
    // Preserve final_mult from parent if it's a START/END entry
    // For SOURCE entries, final_mult will be computed later
}

void transform_init_foreign_temps(entry_t* entry) {
    apply_to_decrypted_entry(entry, init_foreign_temps_op);
}

// ============================================================================
// Distribute-Expand Transform Functions
// ============================================================================

/**
 * Initialize destination index to 0
 */
static void init_dst_idx_op(entry_t* entry) {
    entry->dst_idx = 0;
}

void transform_init_dst_idx(entry_t* entry) {
    apply_to_decrypted_entry(entry, init_dst_idx_op);
}

/**
 * Initialize index field to 0
 */
static void init_index_op(entry_t* entry) {
    entry->index = 0;
}

void transform_init_index(entry_t* entry) {
    apply_to_decrypted_entry(entry, init_index_op);
}

/**
 * Mark entries with final_mult = 0 as DIST_PADDING
 */
static void mark_zero_mult_padding_op(entry_t* entry) {
    // Use oblivious selection to avoid branching
    int is_zero = (entry->final_mult == 0);
    entry->field_type = is_zero * DIST_PADDING + (1 - is_zero) * entry->field_type;
}

void transform_mark_zero_mult_padding(entry_t* entry) {
    apply_to_decrypted_entry(entry, mark_zero_mult_padding_op);
}

/**
 * Create a distribution padding entry
 */
static void create_dist_padding_op(entry_t* entry) {
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
    apply_to_decrypted_entry(entry, create_dist_padding_op);
}

// ============================================================================
// Align-Concat Transform Functions
// ============================================================================

/**
 * Initialize copy index to 0
 */
static void init_copy_index_op(entry_t* entry) {
    entry->copy_index = 0;
}

void transform_init_copy_index(entry_t* entry) {
    apply_to_decrypted_entry(entry, init_copy_index_op);
}

/**
 * Compute alignment key = foreign_sum + (copy_index / local_mult)
 */
static void compute_alignment_key_op(entry_t* entry) {
    // Avoid division by zero obliviously
    int32_t safe_local_mult = entry->local_mult + (entry->local_mult == 0);
    entry->alignment_key = entry->foreign_sum + (entry->copy_index / safe_local_mult);
}

void transform_compute_alignment_key(entry_t* entry) {
    apply_to_decrypted_entry(entry, compute_alignment_key_op);
}

/**
 * Set join_attr from a specific column (Algorithm 999)
 * Extract the value from attributes[column_index] and set as join_attr
 */
static void set_join_attr_op(entry_t* entry, int32_t column_index) {
    // Validate column index
    if (column_index >= 0 && column_index < MAX_ATTRIBUTES) {
        entry->join_attr = entry->attributes[column_index];
        
        // Debug: Check if the join_attr is within design constraints
        if (entry->join_attr < INT32_MIN/2 || entry->join_attr > INT32_MAX/2) {
            DEBUG_WARN("set_join_attr: value %d from attributes[%d] is outside design range [%d, %d]", 
                       entry->join_attr, column_index, INT32_MIN/2, INT32_MAX/2);
        }
        DEBUG_TRACE("set_join_attr: column_index=%d, value=%d", column_index, entry->join_attr);
    } else {
        // Invalid index, set to 0 as default
        entry->join_attr = 0;
        DEBUG_WARN("set_join_attr: invalid column_index=%d", column_index);
    }
}

void transform_set_join_attr(entry_t* entry, int32_t column_index) {
    if (!entry) return;
    
    uint8_t was_encrypted = entry->is_encrypted;
    
    // Decrypt if needed
    if (was_encrypted) {
        crypto_status_t status = aes_decrypt_entry(entry);
        if (status != CRYPTO_SUCCESS) {
            return;
        }
    }
    
    // Apply the operation
    set_join_attr_op(entry, column_index);
    
    // Re-encrypt if it was encrypted
    if (was_encrypted) {
        aes_encrypt_entry(entry);
    }
}

/**
 * Initialize metadata fields to NULL_VALUE based on field mask
 * This provides fine-grained control over which fields to initialize
 */
static uint32_t g_field_mask;

static void init_metadata_null_op(entry_t* entry) {
    // Check each individual mask bit and initialize corresponding fields
    // This allows any subset of fields to be set to NULL_VALUE
    
    // Persistent metadata fields
    if (g_field_mask & METADATA_ORIGINAL_INDEX) {
        entry->original_index = NULL_VALUE;
    }
    if (g_field_mask & METADATA_LOCAL_MULT) {
        entry->local_mult = NULL_VALUE;
    }
    if (g_field_mask & METADATA_FINAL_MULT) {
        entry->final_mult = NULL_VALUE;
    }
    if (g_field_mask & METADATA_FOREIGN_SUM) {
        entry->foreign_sum = NULL_VALUE;
    }
    
    // Temporary metadata fields
    if (g_field_mask & METADATA_LOCAL_CUMSUM) {
        entry->local_cumsum = NULL_VALUE;
    }
    if (g_field_mask & METADATA_LOCAL_INTERVAL) {
        entry->local_interval = NULL_VALUE;
    }
    if (g_field_mask & METADATA_FOREIGN_INTERVAL) {
        entry->foreign_interval = NULL_VALUE;
    }
    if (g_field_mask & METADATA_LOCAL_WEIGHT) {
        entry->local_weight = NULL_VALUE;
    }
    
    // Distribution metadata fields
    if (g_field_mask & METADATA_DST_IDX) {
        entry->dst_idx = NULL_VALUE;
    }
    if (g_field_mask & METADATA_INDEX) {
        entry->index = NULL_VALUE;
    }
    
    // Alignment metadata fields
    if (g_field_mask & METADATA_COPY_INDEX) {
        entry->copy_index = NULL_VALUE;
    }
    if (g_field_mask & METADATA_ALIGNMENT_KEY) {
        entry->alignment_key = NULL_VALUE;
    }
    
    // Type field - set to NULL_VALUE like other metadata
    if (g_field_mask & METADATA_FIELD_TYPE) {
        // Set to NULL_VALUE for clarity in debugging
        // The algorithm will explicitly set these when needed
        entry->field_type = NULL_VALUE;
        entry->equality_type = NULL_VALUE;
    }
}

void transform_init_metadata_null(entry_t* entry, uint32_t field_mask) {
    g_field_mask = field_mask;
    apply_to_decrypted_entry(entry, init_metadata_null_op);
}