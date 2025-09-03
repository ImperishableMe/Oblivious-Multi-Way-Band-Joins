#include "../enclave_types.h"
#include "../../../common/comparator_convention.h"
#include "../../../common/batch_types.h"
#include <stdint.h>

/**
 * Non-oblivious comparator functions for merge sort
 * 
 * These comparators return 1 if e1 < e2, 0 otherwise.
 * They are used for non-oblivious sorting where data is encrypted
 * and we don't need to hide access patterns.
 * 
 * Each comparator implements the same logic as the oblivious versions
 * but returns a comparison result instead of performing swaps.
 */

/**
 * Helper: Get precedence for entry type combination
 * Same logic as in comparators.c
 */
static inline int32_t get_precedence(entry_type_t field_type, equality_type_t equality_type) {
    // (END, NEQ) -> 1    // Open end: exclude boundary, comes first
    // (START, EQ) -> 1   // Closed start: include boundary, comes first
    // (SOURCE, _) -> 2   // Source entries in middle
    // (END, EQ) -> 3     // Closed end: include boundary, comes last
    // (START, NEQ) -> 3  // Open start: exclude boundary, comes last
    
    int32_t is_start_neq = ((field_type == START) & (equality_type == NEQ));
    int32_t is_end_eq = ((field_type == END) & (equality_type == EQ));
    int32_t is_source = (field_type == SOURCE);
    int32_t is_start_eq = ((field_type == START) & (equality_type == EQ));
    int32_t is_end_neq = ((field_type == END) & (equality_type == NEQ));
    
    return 1 * (is_end_neq | is_start_eq) + 
           2 * is_source + 
           3 * (is_end_eq | is_start_neq);
}

/**
 * Compare by join attribute
 * Returns 1 if e1 < e2, 0 otherwise
 */
int compare_join_attr(entry_t* e1, entry_t* e2) {
    // Handle SORT_PADDING - always goes to end
    if (e1->field_type == SORT_PADDING && e2->field_type != SORT_PADDING) {
        return 0;  // e1 (padding) >= e2 (not padding)
    }
    if (e1->field_type != SORT_PADDING && e2->field_type == SORT_PADDING) {
        return 1;  // e1 (not padding) < e2 (padding)
    }
    
    // Compare join attributes
    if (e1->join_attr != e2->join_attr) {
        return (e1->join_attr < e2->join_attr) ? 1 : 0;
    }
    
    // If equal, use precedence
    int32_t p1 = get_precedence(e1->field_type, e1->equality_type);
    int32_t p2 = get_precedence(e2->field_type, e2->equality_type);
    return (p1 < p2) ? 1 : 0;
}

/**
 * Compare for pairwise processing
 * Priority: 1) TARGET before SOURCE, 2) by original_index, 3) START before END
 */
int compare_pairwise(entry_t* e1, entry_t* e2) {
    // Handle SORT_PADDING
    if (e1->field_type == SORT_PADDING && e2->field_type != SORT_PADDING) {
        return 0;
    }
    if (e1->field_type != SORT_PADDING && e2->field_type == SORT_PADDING) {
        return 1;
    }
    
    // Check if entries are TARGET type (START or END)
    int32_t is_target1 = ((e1->field_type == START) | (e1->field_type == END));
    int32_t is_target2 = ((e2->field_type == START) | (e2->field_type == END));
    
    // Priority 1: TARGET entries before SOURCE
    if (is_target1 != is_target2) {
        return is_target1 ? 1 : 0;  // TARGET comes first
    }
    
    // Priority 2: Compare by original index
    if (e1->original_index != e2->original_index) {
        return (e1->original_index < e2->original_index) ? 1 : 0;
    }
    
    // Priority 3: START before END for same index
    if (e1->field_type == START && e2->field_type == END) {
        return 1;  // START comes first
    }
    if (e1->field_type == END && e2->field_type == START) {
        return 0;  // END comes after
    }
    
    return 0;  // Equal
}

/**
 * Compare with END entries first
 * Priority: 1) END before others, 2) by original_index
 */
int compare_end_first(entry_t* e1, entry_t* e2) {
    // Handle SORT_PADDING
    if (e1->field_type == SORT_PADDING && e2->field_type != SORT_PADDING) {
        return 0;
    }
    if (e1->field_type != SORT_PADDING && e2->field_type == SORT_PADDING) {
        return 1;
    }
    
    // Check if entries are END type
    int32_t is_end1 = (e1->field_type == END);
    int32_t is_end2 = (e2->field_type == END);
    
    // Priority 1: END entries before all others
    if (is_end1 != is_end2) {
        return is_end1 ? 1 : 0;  // END comes first
    }
    
    // Priority 2: Compare by original index
    return (e1->original_index < e2->original_index) ? 1 : 0;
}

/**
 * Compare by join attr then other attributes
 * Used for final output sorting in align phase
 */
int compare_join_then_other(entry_t* e1, entry_t* e2) {
    // Handle SORT_PADDING
    if (e1->field_type == SORT_PADDING && e2->field_type != SORT_PADDING) {
        return 0;
    }
    if (e1->field_type != SORT_PADDING && e2->field_type == SORT_PADDING) {
        return 1;
    }
    
    // Primary: join_attr
    if (e1->join_attr != e2->join_attr) {
        return (e1->join_attr < e2->join_attr) ? 1 : 0;
    }
    
    // Secondary: Compare attributes lexicographically
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (e1->attributes[i] != e2->attributes[i]) {
            return (e1->attributes[i] < e2->attributes[i]) ? 1 : 0;
        }
    }
    
    return 0;  // Equal
}

/**
 * Compare by original index
 */
int compare_original_index(entry_t* e1, entry_t* e2) {
    // Handle SORT_PADDING
    if (e1->field_type == SORT_PADDING && e2->field_type != SORT_PADDING) {
        return 0;
    }
    if (e1->field_type != SORT_PADDING && e2->field_type == SORT_PADDING) {
        return 1;
    }
    
    return (e1->original_index < e2->original_index) ? 1 : 0;
}

/**
 * Compare by alignment key
 */
int compare_alignment_key(entry_t* e1, entry_t* e2) {
    // Handle SORT_PADDING
    if (e1->field_type == SORT_PADDING && e2->field_type != SORT_PADDING) {
        return 0;
    }
    if (e1->field_type != SORT_PADDING && e2->field_type == SORT_PADDING) {
        return 1;
    }
    
    return (e1->alignment_key < e2->alignment_key) ? 1 : 0;
}

/**
 * Compare with padding last
 * SORT_PADDING and DIST_PADDING go to end
 */
int compare_padding_last(entry_t* e1, entry_t* e2) {
    // Check for padding types
    int32_t is_padding1 = (e1->field_type == SORT_PADDING) | (e1->field_type == DIST_PADDING);
    int32_t is_padding2 = (e2->field_type == SORT_PADDING) | (e2->field_type == DIST_PADDING);
    
    // Padding goes to end
    if (is_padding1 && !is_padding2) {
        return 0;  // e1 (padding) >= e2 (not padding)
    }
    if (!is_padding1 && is_padding2) {
        return 1;  // e1 (not padding) < e2 (padding)
    }
    
    // Both padding or both not padding - compare by original_index
    return (e1->original_index < e2->original_index) ? 1 : 0;
}

/**
 * Compare for distribute phase
 * Sort by dst_idx
 */
int compare_distribute(entry_t* e1, entry_t* e2) {
    // Handle SORT_PADDING
    if (e1->field_type == SORT_PADDING && e2->field_type != SORT_PADDING) {
        return 0;
    }
    if (e1->field_type != SORT_PADDING && e2->field_type == SORT_PADDING) {
        return 1;
    }
    
    return (e1->dst_idx < e2->dst_idx) ? 1 : 0;
}

/**
 * Get comparator function by type
 */
comparator_func_t get_merge_comparator(OpEcall type) {
    switch(type) {
        case OP_ECALL_COMPARATOR_JOIN_ATTR:
            return compare_join_attr;
        case OP_ECALL_COMPARATOR_PAIRWISE:
            return compare_pairwise;
        case OP_ECALL_COMPARATOR_END_FIRST:
            return compare_end_first;
        case OP_ECALL_COMPARATOR_JOIN_THEN_OTHER:
            return compare_join_then_other;
        case OP_ECALL_COMPARATOR_ORIGINAL_INDEX:
            return compare_original_index;
        case OP_ECALL_COMPARATOR_ALIGNMENT_KEY:
            return compare_alignment_key;
        case OP_ECALL_COMPARATOR_PADDING_LAST:
            return compare_padding_last;
        case OP_ECALL_COMPARATOR_DISTRIBUTE:
            return compare_distribute;
        default:
            return compare_join_attr;  // Default fallback
    }
}