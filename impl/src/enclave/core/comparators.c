#include "../enclave_types.h"
#include <stdint.h>
#include <string.h>

/**
 * Comparator functions for oblivious sorting
 * All comparators use oblivious (branchless) operations to prevent
 * information leakage through memory access patterns
 * 
 * NOTE: Unlike the thesis algorithms which return -1/0/1, our implementation
 * directly performs oblivious swaps in-place. This is more efficient for 
 * oblivious execution as it avoids branching in the sorting algorithm.
 * The oblivious_swap always executes (with a mask determining if values 
 * actually change), maintaining constant memory access patterns.
 * 
 * All comparators follow the pattern:
 * 1. Compute comparison result
 * 2. Call oblivious_swap(e1, e2, should_swap) where should_swap = (e1 > e2)
 */

/**
 * Helper: Oblivious ternary functions
 * Returns condition * true_val + (1 - condition) * false_val
 * Ensures branchless execution for constant-time operations
 */
static inline int32_t oblivious_ternary(int32_t condition, int32_t true_val, int32_t false_val) {
    return condition * true_val + (1 - condition) * false_val;
}

static inline uint8_t oblivious_ternary_u8(int32_t condition, uint8_t true_val, uint8_t false_val) {
    return condition * true_val + (1 - condition) * false_val;
}

/**
 * Helper: Oblivious sign function
 * Returns -1, 0, or 1 based on value comparison
 */
static inline int32_t oblivious_sign(int32_t val) {
    return (val > 0) - (val < 0);
}

/**
 * Helper: Get precedence for entry type combination (Algorithm 513)
 * Precedence ordering ensures correct join semantics
 */
static inline int32_t get_precedence(entry_type_t field_type, equality_type_t equality_type) {
    // Precedence table (from Algorithm 513):
    // (START, EQ) -> 1
    // (END, NEQ) -> 1  
    // (SOURCE, _) -> 2
    // (START, NEQ) -> 3
    // (END, EQ) -> 3
    
    int32_t is_start_eq = ((field_type == START) & (equality_type == EQ));
    int32_t is_end_neq = ((field_type == END) & (equality_type == NEQ));
    int32_t is_source = (field_type == SOURCE);
    int32_t is_start_neq = ((field_type == START) & (equality_type == NEQ));
    int32_t is_end_eq = ((field_type == END) & (equality_type == EQ));
    
    return 1 * (is_start_eq | is_end_neq) + 
           2 * is_source + 
           3 * (is_start_neq | is_end_eq);
}

/**
 * Oblivious swap function for entry_t
 * Swaps two entries if should_swap is non-zero
 */
void oblivious_swap(entry_t* e1, entry_t* e2, int should_swap) {
    // Create mask: all 1s if should_swap, all 0s otherwise
    uint8_t mask = oblivious_ternary_u8((should_swap != 0), 0xFF, 0x00);
    
    // XOR swap with mask
    uint8_t* p1 = (uint8_t*)e1;
    uint8_t* p2 = (uint8_t*)e2;
    
    for (size_t i = 0; i < sizeof(entry_t); i++) {
        uint8_t diff = (p1[i] ^ p2[i]) & mask;
        p1[i] ^= diff;
        p2[i] ^= diff;
    }
}

/**
 * Comparator by join attribute (Algorithm 399)
 * Primary: join_attr, Secondary: entry type precedence
 */
void comparator_join_attr(entry_t* e1, entry_t* e2) {
    // Compare join attributes (both are int32_t)
    int32_t diff = e1->join_attr - e2->join_attr;
    int32_t cmp = oblivious_sign(diff);
    
    // Check if equal
    int32_t is_equal = (cmp == 0);
    
    // Get precedence values
    int32_t p1 = get_precedence(e1->field_type, e1->equality_type);
    int32_t p2 = get_precedence(e2->field_type, e2->equality_type);
    int32_t prec_cmp = oblivious_sign(p1 - p2);
    
    // Combine: use join_attr comparison if not equal, else use precedence
    int32_t result = (1 - is_equal) * cmp + is_equal * prec_cmp;
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator for pairwise processing (Algorithm 438)
 * Priority: 1) TARGET before SOURCE, 2) by original_index, 3) START before END
 */
void comparator_pairwise(entry_t* e1, entry_t* e2) {
    // Check if entries are TARGET type (START or END)
    int32_t is_target1 = ((e1->field_type == START) | (e1->field_type == END));
    int32_t is_target2 = ((e2->field_type == START) | (e2->field_type == END));
    
    // Priority 1: TARGET entries before SOURCE
    int32_t type_priority = is_target2 - is_target1;  // Negative if e1 is target
    
    // Priority 2: Compare by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Priority 3: START before END for same index
    int32_t is_start1 = (e1->field_type == START);
    int32_t is_start2 = (e2->field_type == START);
    int32_t start_first = is_start1 - is_start2;  // Negative if e1 is START (follows thesis exactly)
    
    // Check if priorities are equal at each level
    int32_t same_priority = (type_priority == 0);
    int32_t same_index = (idx_cmp == 0);
    
    // Combine priorities hierarchically
    int32_t result = (1 - same_priority) * type_priority +
                     same_priority * (1 - same_index) * idx_cmp +
                     same_priority * same_index * start_first;
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator with END entries first (Algorithm 479)
 * Priority: 1) END before others, 2) by original_index
 */
void comparator_end_first(entry_t* e1, entry_t* e2) {
    // Check if entries are END type
    int32_t is_end1 = (e1->field_type == END);
    int32_t is_end2 = (e2->field_type == END);
    
    // Priority 1: END entries before all others
    int32_t type_priority = is_end2 - is_end1;  // Negative if e1 is END
    
    // Priority 2: Compare by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Check if type priority is equal
    int32_t same_type = (type_priority == 0);
    
    // Combine priorities
    int32_t result = (1 - same_type) * type_priority + same_type * idx_cmp;
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator by join attribute then original index (Algorithm 697)
 * Primary: join_attr, Secondary: original_index
 */
void comparator_join_then_other(entry_t* e1, entry_t* e2) {
    // Compare join attributes (both are int32_t)
    int32_t diff = e1->join_attr - e2->join_attr;
    int32_t cmp = oblivious_sign(diff);
    
    // Check if equal
    int32_t is_equal = (cmp == 0);
    
    // Compare by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Combine: use join_attr comparison if not equal, else use index
    int32_t result = (1 - is_equal) * cmp + is_equal * idx_cmp;
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator by original index only
 * Simple comparison for maintaining original order
 */
void comparator_original_index(entry_t* e1, entry_t* e2) {
    // Compare original indices
    int32_t result = oblivious_sign(e1->original_index - e2->original_index);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator by alignment key (Algorithm 715)
 * Used in final alignment phase
 */
void comparator_alignment_key(entry_t* e1, entry_t* e2) {
    // Compare alignment keys
    int32_t result = oblivious_sign(e1->alignment_key - e2->alignment_key);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}