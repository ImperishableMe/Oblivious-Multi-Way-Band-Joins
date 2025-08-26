#ifndef ENCLAVE_TYPES_H
#define ENCLAVE_TYPES_H

#include "../common/constants.h"
#include "../common/types_common.h"

// Entry structure for enclave processing
typedef struct {
    // Entry metadata
    entry_type_t field_type;      // EMPTY, SOURCE, START, END, TARGET
    equality_type_t equality_type; // EQ, NEQ, NONE
    bool is_encrypted;            // Whether data is encrypted
    
    // Join attribute
    double join_attr;
    
    // Persistent metadata (persists across phases)
    uint32_t original_index;      // Original position in table
    uint32_t local_mult;          // Local multiplicity
    uint32_t final_mult;          // Final multiplicity
    uint32_t foreign_sum;         // Foreign sum for alignment
    
    // Temporary metadata (reused between phases)
    uint32_t local_cumsum;        // Cumulative sum (bottom-up)
    uint32_t local_interval;      // Interval value (bottom-up)
    uint32_t foreign_cumsum;      // Foreign cumulative sum (top-down)
    uint32_t foreign_interval;    // Foreign interval (top-down)
    uint32_t local_weight;        // Local weight counter (top-down)
    
    // Expansion metadata
    uint32_t copy_index;          // Which copy of the tuple (0 to final_mult-1)
    uint32_t alignment_key;       // Key for alignment phase
    
    // Data attributes
    double attributes[MAX_ATTRIBUTES];
    char column_names[MAX_ATTRIBUTES][MAX_COLUMN_NAME_LEN];
} entry_t;

#endif // ENCLAVE_TYPES_H