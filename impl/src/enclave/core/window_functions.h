#ifndef WINDOW_FUNCTIONS_H
#define WINDOW_FUNCTIONS_H

#include "../enclave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Window functions for multiplicity computation */

/**
 * Algorithm 4.8: Window Set Original Index
 * 
 * From thesis section 4.2 (Initialization):
 * "The initialization phase prepares the join tree for multiplicity computation by
 * transforming input tables into augmented tables with empty metadata columns using
 * the Map primitive. We then use LinearPass to assign sequential original indices.
 * This demonstrates the stateless window-based approach where each tuple's index
 * is computed from its predecessor in the sliding window."
 * 
 * PSEUDOCODE (Algorithm 4.8):
 * Function WindowSetOriginalIndex(window)
 *     window[1].field_index ← window[0].field_index + 1
 * EndFunction
 * 
 * COMPLEXITY: O(1) per window operation
 * OBLIVIOUS PROPERTY: Access pattern is fixed and data-independent
 */
void window_set_original_index(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.10: Window Compute Local Sum
 * 
 * From thesis section 4.3 (Phase 1: Bottom-Up Multiplicity Computation):
 * "We apply WindowComputeLocalSum via a linear pass to maintain a running sum of
 * local multiplicities: the sum increases by α_local when we encounter SOURCE entries,
 * and the current sum gets recorded when we hit START/END boundaries."
 * 
 * PSEUDOCODE (Algorithm 4.10):
 * Function WindowComputeLocalSum(window)
 *     If window[1].field_type = SOURCE
 *         window[1].local_cumsum ← window[0].local_cumsum + window[1].local_mult
 *     Else  // window[1].field_type ∈ {START, END}
 *         window[1].local_cumsum ← window[0].local_cumsum
 *     EndIf
 * EndFunction
 * 
 * KEY INSIGHT: This transforms range matching into cumulative sum computation through
 * the dual-entry technique. For any target entry that derives boundary entries (START
 * and END), the set of source entries appearing between them in sorted order is exactly
 * the set of source entries that satisfy the join condition with the target entry.
 * 
 * OBLIVIOUS PROPERTY: Conditional execution without data-dependent branching
 */
void window_compute_local_sum(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.12: Window Compute Local Interval
 * 
 * From thesis section 4.3 (Phase 1: Bottom-Up Multiplicity Computation):
 * "We apply WindowComputeLocalInterval via a linear pass to compute the difference
 * between each pair's cumulative sums, yielding the local interval that represents
 * the local multiplicity contribution from the child's subtree for that target tuple."
 * 
 * PSEUDOCODE (Algorithm 4.12):
 * Function WindowComputeLocalInterval(window)
 *     If window[0].field_type = START and window[1].field_type = END
 *         window[1].local_interval ← window[1].local_cumsum - window[0].local_cumsum
 *     EndIf
 * EndFunction
 * 
 * CONTEXT: After ComparatorPairwise sorts START and END pairs to be adjacent, this
 * function computes the sum of local multiplicities for all SOURCE entries that fall
 * within the range defined by the START/END boundaries.
 * 
 * MATHEMATICAL MEANING: For parent tuple t_v, this computes:
 * Σ{t_c ∈ R_c : (t_v, t_c) satisfy constraint(v,c)} t_c.local_mult
 */
void window_compute_local_interval(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.15: Window Compute Foreign Sum
 * 
 * From thesis section 4.4 (Phase 2: Top-Down Final Multiplicity Propagation):
 * "We apply WindowComputeForeignSum via a linear pass that simultaneously tracks two
 * counters. When we encounter START/END boundaries, we update the local weight by
 * adding or subtracting the child tuple's local multiplicity. When we encounter SOURCE
 * entries (parent tuples), we increment the foreign cumulative sum by the parent's
 * final multiplicity divided by the current local weight."
 * 
 * PSEUDOCODE (Algorithm 4.15):
 * Function WindowComputeForeignSum(window)
 *     If window[1].field_type = START
 *         window[1].local_weight ← window[0].local_weight + window[1].local_mult
 *         window[1].foreign_cumsum ← window[0].foreign_cumsum
 *     ElseIf window[1].field_type = END
 *         window[1].local_weight ← window[0].local_weight - window[1].local_mult
 *         window[1].foreign_cumsum ← window[0].foreign_cumsum
 *     ElseIf window[1].field_type = SOURCE
 *         window[1].local_weight ← window[0].local_weight
 *         window[1].foreign_cumsum ← window[0].foreign_cumsum + 
 *                                    window[1].final_mult / window[1].local_weight
 *     EndIf
 * EndFunction
 * 
 * KEY INSIGHT: The division final_mult/local_weight recovers the parent's multiplicity
 * in T \ T_c (the join result excluding the child's subtree). The accumulation gives
 * each child tuple its foreign multiplicity sum.
 * 
 * DUAL COUNTER TECHNIQUE: local_weight tracks sum of matching child local_mult values,
 * while foreign_cumsum accumulates the foreign contributions.
 */
void window_compute_foreign_sum(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.16: Window Compute Foreign Interval
 * 
 * From thesis section 4.4 (Phase 2: Top-Down Final Multiplicity Propagation):
 * "Similar to the bottom-up phase, we compute the foreign interval as the difference
 * between START and END cumulative sums. Additionally, we record the foreign_sum
 * which serves as the alignment key during result construction."
 * 
 * PSEUDOCODE (Algorithm 4.16):
 * Function WindowComputeForeignInterval(window)
 *     If window[0].field_type = START and window[1].field_type = END
 *         foreign_interval ← window[1].foreign_cumsum - window[0].foreign_cumsum
 *         window[1].foreign_interval ← foreign_interval
 *         window[1].foreign_sum ← window[0].foreign_cumsum  // Record alignment position
 *     EndIf
 * EndFunction
 * 
 * DUAL PURPOSE: The foreign_sum serves both for computing final_mult = local_mult × 
 * foreign_interval and as the alignment key during Phase 4.
 */
void window_compute_foreign_interval(entry_t* e1, entry_t* e2);

/**
 * Algorithm 4.13: Update Target Multiplicity
 * 
 * From thesis section 4.3 (Phase 1: Bottom-Up Multiplicity Computation):
 * "After creating and sorting the combined table, we apply UpdateTargetMultiplicity
 * via a parallel pass to propagate the computed intervals back to the parent table,
 * multiplying each target tuple's existing local multiplicity by the contribution
 * from this child (the interval value) to produce the updated local multiplicities."
 * 
 * PSEUDOCODE (Algorithm 4.13):
 * Function UpdateTargetMultiplicity(e_combined, e_target)
 *     e_target.local_mult ← e_target.local_mult × e_combined.local_interval
 * EndFunction
 * 
 * PARALLEL PASS: This uses ParallelPass utility to process aligned pairs from two
 * tables of same size, where e_combined contains computed intervals and e_target
 * contains the original target table entries.
 * 
 * MATHEMATICAL MEANING: Updates t_v.local_mult^{new} = t_v.local_mult^{old} × 
 * Σ{t_c ∈ R_c : (t_v, t_c) satisfy constraint(v,c)} t_c.local_mult
 */
void update_target_multiplicity(entry_t* e_combined, entry_t* e_target);

/**
 * Algorithm 4.17: Update Target Final Multiplicity
 * 
 * From thesis section 4.4 (Phase 2: Top-Down Final Multiplicity Propagation):
 * "We use a parallel pass to propagate foreign intervals to compute final multiplicities.
 * Each child tuple's final multiplicity equals its local multiplicity times its foreign
 * multiplicity, where the foreign multiplicity represents the number of join results
 * from tables outside the child's subtree that connect through the parent."
 * 
 * PSEUDOCODE (Algorithm 4.17):
 * Function UpdateTargetFinalMultiplicity(e, t)
 *     t.final_mult ← e.foreign_interval × t.local_mult
 *     t.foreign_sum ← e.foreign_sum  // For alignment
 * EndFunction
 * 
 * KEY RELATIONSHIP: final_mult = local_mult × foreign_mult
 * - local_mult: contribution from child's subtree
 * - foreign_mult (foreign_interval): contribution from outside child's subtree
 * 
 * ALIGNMENT PREPARATION: The foreign_sum is preserved for use in Phase 4 where it
 * determines the alignment position for each tuple in the final join result.
 */
void update_target_final_multiplicity(entry_t* e_combined, entry_t* e_target);

#ifdef __cplusplus
}
#endif

#endif // WINDOW_FUNCTIONS_H