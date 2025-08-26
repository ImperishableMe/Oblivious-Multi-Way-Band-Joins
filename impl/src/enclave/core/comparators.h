#ifndef COMPARATORS_H
#define COMPARATORS_H

#include "../enclave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Comparator functions for oblivious sorting */
/* All comparators obliviously swap entries if e1 > e2 */

/**
 * Algorithm 4.9: Comparator Join Attribute
 * 
 * From thesis section 4.3 (Phase 1: Bottom-Up Multiplicity Computation):
 * "We then sort by ComparatorJoinAttr, which orders entries primarily by join attribute
 * value and secondarily by a precedence based on entry type and equality type. The
 * precedence ordering ensures that (START, EQ) and (END, NEQ) entries come first with
 * precedence 1, SOURCE entries have precedence 2, and (START, NEQ) and (END, EQ) entries
 * come last with precedence 3. This careful ordering guarantees that for any target
 * entry that derives boundary entries (start_entry and stop_entry), the set of source
 * entries appearing between them in the sorted order is exactly the set of source
 * entries that satisfy the join condition with the target entry."
 * 
 * PSEUDOCODE (Algorithm 4.9):
 * Function ComparatorJoinAttr(e1, e2)
 *     If e1.join_attr < e2.join_attr
 *         Return -1
 *     ElseIf e1.join_attr > e2.join_attr
 *         Return 1
 *     Else
 *         p1 <- GetPrecedence((e1.field_type, e1.field_equality_type))
 *         p2 <- GetPrecedence((e2.field_type, e2.field_equality_type))
 *         If p1 < p2
 *             Return -1
 *         ElseIf p1 > p2
 *             Return 1
 *         Else
 *             Return 0
 *         EndIf
 *     EndIf
 * EndFunction
 * 
 * KEY INSIGHT: The precedence ordering ensures correct range matching:
 * - (START, EQ) at value v includes all SOURCE entries >= v
 * - (START, NEQ) at value v includes all SOURCE entries > v
 * - (END, EQ) at value v includes all SOURCE entries <= v
 * - (END, NEQ) at value v includes all SOURCE entries < v
 */
void comparator_join_attr(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.11: Comparator Pairwise
 * 
 * From thesis section 4.3 (Phase 1: Bottom-Up Multiplicity Computation):
 * "We then sort by ComparatorPairwise to place START and END pairs (which originated
 * from the same target tuple) next to each other. This comparator first groups all
 * TARGET entries (START/END) before SOURCE entries, then sorts by original_index,
 * and finally ensures START comes before END for the same index."
 * 
 * PSEUDOCODE (Algorithm 4.11):
 * Function ComparatorPairwise(e1, e2)
 *     // First: Target entries (START/END) before SOURCE entries
 *     If e1.field_type in {START, END} and e2.field_type = SOURCE
 *         Return -1
 *     ElseIf e1.field_type = SOURCE and e2.field_type in {START, END}
 *         Return 1
 *     EndIf
 *     // Second: Sort by original index
 *     If e1.field_index < e2.field_index
 *         Return -1
 *     ElseIf e1.field_index > e2.field_index
 *         Return 1
 *     EndIf
 *     // Third: START before END for same index
 *     If e1.field_type = START and e2.field_type = END
 *         Return -1
 *     ElseIf e1.field_type = END and e2.field_type = START
 *         Return 1
 *     Else
 *         Return 0
 *     EndIf
 * EndFunction
 * 
 * PURPOSE: Groups START/END pairs from the same original target tuple to be adjacent,
 * enabling WindowComputeLocalInterval to compute their difference.
 */
void comparator_pairwise(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.14: Comparator End First
 * 
 * From thesis section 4.3 (Phase 1: Bottom-Up Multiplicity Computation):
 * "Finally, we sort by ComparatorEndFirst to ensure END entries appear first,
 * ordered by their original index. This prepares for the parallel pass where
 * we extract the first |R_target| entries (all END entries) and align them
 * with the original target table."
 * 
 * PSEUDOCODE (Algorithm 4.14):
 * Function ComparatorEndFirst(e1, e2)
 *     // First: END entries before all others
 *     If e1.field_type = END and e2.field_type != END
 *         Return -1
 *     ElseIf e1.field_type != END and e2.field_type = END
 *         Return 1
 *     EndIf
 *     // Second: Sort by original index
 *     If e1.field_index < e2.field_index
 *         Return -1
 *     ElseIf e1.field_index > e2.field_index
 *         Return 1
 *     Else
 *         Return 0
 *     EndIf
 * EndFunction
 * 
 * CONTEXT: After WindowComputeLocalInterval, END entries contain the computed
 * local_interval values. This sort brings them to the front for extraction.
 */
void comparator_end_first(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.19: Join Then Other Attributes Comparator
 * 
 * From thesis section 4.6 (Phase 4: Alignment and Concatenation):
 * "The parent table is sorted by join attributes (and secondarily by other attributes
 * for deterministic ordering), creating groups of identical tuples. Each group
 * represents a distinct combination from the parent table that will be matched with
 * corresponding child tuples."
 * 
 * PSEUDOCODE (Algorithm 4.19):
 * Function JoinThenOtherAttributes(t1, t2)
 *     If t1.join_attr < t2.join_attr
 *         Return -1
 *     ElseIf t1.join_attr > t2.join_attr
 *         Return 1
 *     Else
 *         // Compare by parent's original index (maintained in R_accumulator)
 *         Return CompareOriginalIndex(t1, t2)
 *     EndIf
 * EndFunction
 * 
 * PURPOSE: Creates deterministic groups of parent tuples for alignment phase.
 * Secondary sorting by original_index ensures consistent ordering within groups.
 */
void comparator_join_then_other(entry_t* e1, entry_t* e2);

/**
 * Algorithm: Compare Original Index (referenced in Algorithm 4.19)
 * 
 * From thesis: Used as secondary comparison in JoinThenOtherAttributes
 * to ensure deterministic ordering within groups of identical join attributes.
 * 
 * PSEUDOCODE:
 * Function CompareOriginalIndex(t1, t2)
 *     If t1.original_index < t2.original_index
 *         Return -1
 *     ElseIf t1.original_index > t2.original_index
 *         Return 1
 *     Else
 *         Return 0
 *     EndIf
 * EndFunction
 * 
 * OBLIVIOUS PROPERTY: Simple integer comparison without data-dependent branching
 */
void comparator_original_index(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.20: Alignment Key Comparator
 * 
 * From thesis section 4.6 (Phase 4: Alignment and Concatenation):
 * "The child table alignment uses the formula foreign_sum + (copy_index / local_mult),
 * where foreign_sum is the index of the first parent group that matches this child
 * tuple, copy_index is the index of this copy among all copies of the same original
 * tuple (0 to final_mult-1), and local_mult is the child tuple's local multiplicity.
 * This formula ensures that every local_mult copies of a child tuple increment to the
 * next parent group, correctly distributing child copies across matching parent groups."
 * 
 * PSEUDOCODE (Algorithm 4.20):
 * Function AlignmentKeyComparator(t1, t2)
 *     If t1.alignment_key < t2.alignment_key
 *         Return -1
 *     ElseIf t1.alignment_key > t2.alignment_key
 *         Return 1
 *     Else
 *         Return 0
 *     EndIf
 * EndFunction
 * 
 * NOTE: The alignment_key is pre-computed as:
 * alignment_key = foreign_sum + (copy_index / local_mult)
 * 
 * KEY INSIGHT: This ensures child tuple copies are distributed correctly across
 * parent groups, maintaining the join relationship after expansion.
 */
void comparator_alignment_key(entry_t* e1, entry_t* e2);

/* Helper functions */

/**
 * Algorithm 4.14: Get Entry Type Precedence
 * 
 * From thesis section 4.3 (Phase 1: Bottom-Up Multiplicity Computation):
 * "The precedence ordering (defined by GetPrecedence) ensures that (START, EQ) and
 * (END, NEQ) entries come first with precedence 1, SOURCE entries have precedence 2,
 * and (START, NEQ) and (END, EQ) entries come last with precedence 3."
 * 
 * PSEUDOCODE (Algorithm 4.14):
 * Function GetPrecedence((entry_type, equality_type))
 *     If (entry_type, equality_type) = (START, EQ)
 *         Return 1
 *     ElseIf (entry_type, equality_type) = (END, NEQ)
 *         Return 1
 *     ElseIf (entry_type, equality_type) = (SOURCE, null)
 *         Return 2
 *     ElseIf (entry_type, equality_type) = (START, NEQ)
 *         Return 3
 *     ElseIf (entry_type, equality_type) = (END, EQ)
 *         Return 3
 *     EndIf
 * EndFunction
 * 
 * RATIONALE: This ordering ensures that:
 * - Closed lower bounds (START, EQ) and open upper bounds (END, NEQ) come first
 * - SOURCE entries come in the middle
 * - Open lower bounds (START, NEQ) and closed upper bounds (END, EQ) come last
 * This guarantees correct range matching for band joins.
 */
int get_precedence(entry_type_t field_type, equality_type_t equality_type);

/**
 * Oblivious Swap Primitive
 * 
 * From thesis section 4.1.4 (Common Utilities):
 * "ObliviousSort utility is the foundation of our approach, utilizing predetermined
 * comparison networks to sort tables with fixed access patterns that remain independent
 * of actual data values. The sorting network's structure is determined solely by the
 * input size, ensuring that the sequence of comparisons and swaps follows the same
 * pattern regardless of the data being sorted."
 * 
 * IMPLEMENTATION NOTE: Uses conditional move operations or arithmetic tricks to
 * swap without branching, ensuring constant-time execution regardless of condition.
 * 
 * OBLIVIOUS PROPERTY: The memory access pattern is identical whether swap occurs
 * or not, preventing information leakage through cache timing or memory access patterns.
 */
void oblivious_swap(entry_t* e1, entry_t* e2, bool should_swap);

/**
 * Compare two double values obliviously
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 */
int oblivious_compare_double(double a, double b);

/**
 * Compare two uint32_t values obliviously
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 */
int oblivious_compare_uint32(uint32_t a, uint32_t b);

#ifdef __cplusplus
}
#endif

#endif // COMPARATORS_H