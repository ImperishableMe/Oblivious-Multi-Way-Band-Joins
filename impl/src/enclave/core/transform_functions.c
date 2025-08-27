#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "crypto_helpers.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>

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
static void initialize_leaf_op(entry_t* entry) {
    entry->local_mult = 1;
    entry->final_mult = 0;  // Initialize to 0
}

/**
 * Initialize leaf multiplicities
 * Sets local_mult = 1 for leaf nodes in the join tree
 */
void transform_initialize_leaf(entry_t* entry) {
    apply_to_decrypted_entry(entry, initialize_leaf_op);
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
    entry->foreign_cumsum = 0;
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
    entry->join_attr = entry->join_attr + g_deviation;
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
    entry->join_attr = entry->join_attr + g_deviation;
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
 * NEW: Transform entry to EMPTY type (for padding)
 */
static void set_empty_op(entry_t* entry) {
    entry->field_type = EMPTY;
    entry->join_attr = INT32_MAX;  // Sort to end
    entry->original_index = UINT32_MAX;
    entry->local_mult = 0;
    entry->final_mult = 0;
    // Clear other fields
    entry->foreign_sum = 0;
    entry->local_cumsum = 0;
    entry->local_interval = 0;
    entry->foreign_cumsum = 0;
    entry->foreign_interval = 0;
    entry->local_weight = 0;
}

void transform_set_empty(entry_t* entry) {
    apply_to_decrypted_entry(entry, set_empty_op);
}