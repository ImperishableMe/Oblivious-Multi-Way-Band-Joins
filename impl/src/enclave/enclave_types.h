#ifndef ENCLAVE_TYPES_H
#define ENCLAVE_TYPES_H

#include "../common/constants.h"
#include "../common/types_common.h"
#include <stdint.h>
#include <limits.h>

// Define infinity values and safe range for join attributes
// We limit the valid range to approximately half of int32_t to prevent overflow
// when adding deviations to join attributes
#define JOIN_ATTR_MIN     (-1073741820)         // Minimum valid join attribute
#define JOIN_ATTR_MAX     (1073741820)          // Maximum valid join attribute  
#define JOIN_ATTR_NEG_INF (-1073741821)         // Represents -∞ (just below valid range)
#define JOIN_ATTR_POS_INF (1073741821)          // Represents +∞ (just above valid range)

// This ensures that for any valid join_attr in [JOIN_ATTR_MIN, JOIN_ATTR_MAX]
// and any deviation in the same range, join_attr + deviation will never overflow
// The infinity values are just outside the valid range, making them easy to detect
// Since we never perform inf + inf operations, this approach is safe

// Entry structure for enclave processing
typedef struct {
    // Entry metadata
    entry_type_t field_type;      // SORT_PADDING, SOURCE, START, END, TARGET, DIST_PADDING
    equality_type_t equality_type; // EQ, NEQ, NONE
    uint8_t is_encrypted;         // Whether data is encrypted (0 or 1)
    
    // Encryption nonce for AES-CTR mode
    uint64_t nonce;               // Unique nonce for each encryption
    
    // Join attribute (using int32_t for signed arithmetic)
    int32_t join_attr;
    
    // Persistent metadata (persists across phases)
    int32_t original_index;      // Original position in table
    int32_t local_mult;          // Local multiplicity
    int32_t final_mult;          // Final multiplicity
    int32_t foreign_sum;         // Foreign sum for alignment
    
    // Temporary metadata (reused between phases)
    int32_t local_cumsum;        // Cumulative sum (bottom-up)
    int32_t local_interval;      // Interval value (bottom-up)
    int32_t foreign_cumsum;      // Foreign cumulative sum (top-down)
    int32_t foreign_interval;    // Foreign interval (top-down)
    int32_t local_weight;        // Local weight counter (top-down)
    
    // Expansion metadata
    int32_t copy_index;          // Which copy of the tuple (0 to final_mult-1)
    int32_t alignment_key;       // Key for alignment phase
    
    // Distribution fields
    int32_t dst_idx;             // Destination index for distribution
    int32_t index;               // Current position (0 to output_size-1)
    
    // Data attributes (all integers for our use case)
    int32_t attributes[MAX_ATTRIBUTES];
    char column_names[MAX_ATTRIBUTES][MAX_COLUMN_NAME_LEN];
} entry_t;

#endif // ENCLAVE_TYPES_H