#ifndef PHASE1_BOTTOM_UP_H
#define PHASE1_BOTTOM_UP_H

#include <vector>
#include "../types.h"
#include "../../enclave/enclave_types.h"

/**
 * PHASE 1: BOTTOM-UP MULTIPLICITY COMPUTATION
 * ============================================
 * 
 * From thesis section 4.3:
 * "The bottom-up phase computes local multiplicities (±_local) by traversing the join
 * tree T in post-order. For leaf nodes, we initialize each tuple t  R_leaf with
 * t.local_mult = 1. For non-leaf nodes, the algorithm processes each parent-child
 * pair (v, c) where v is the parent and c  children(v)."
 * 
 * KEY INSIGHT:
 * "At any point during the traversal, for each visited node v, each tuple t  R_v has
 * t.local_mult equal to the number of join results it participates in when considering
 * only the portion of the subtree rooted at v that has been visited so far."
 * 
 * DUAL-ENTRY TECHNIQUE:
 * "The core innovation lies in the dual-entry technique for handling band join constraints.
 * The CombineTable function creates two boundary markers for each tuple in the target
 * (parent) table---START and END entries---that mark where the matching range begins and
 * ends. For example, if a parent tuple with value 10 matches child tuples between values
 * 8 and 12, CombineTable creates a START entry at 8 and an END entry at 12, then combines
 * these boundary entries with the source (child) tuples into a single table."
 */

/**
 * Algorithm 4.5: Bottom-Up Phase
 * 
 * PSEUDOCODE:
 * Function BottomUpPhase(T, root)
 *     order ê PostOrderTraversal(T, root)
 *     ForAll nodes v in order
 *         If v is a leaf
 *             ForAll tuple t  R_v
 *                 t.local_mult ê 1
 *             EndFor
 *         Else
 *             ForAll child nodes c of v
 *                 R_v ê ComputeLocalMultiplicities(R_v, R_c, constraint(v,c))
 *             EndFor
 *         EndIf
 *     EndFor
 *     Return T
 * EndFunction
 * 
 * COMPLEXITY: O(£_{(v,c)  E} (|R_v| + |R_c|) log≤(|R_v| + |R_c|))
 */
void bottom_up_phase(std::vector<Table>& tables, 
                     const std::vector<JoinCondition>& join_conditions,
                     const std::vector<std::vector<int>>& tree_structure);

/**
 * Algorithm 4.6: Post-Order Traversal
 * 
 * PSEUDOCODE:
 * Function PostOrderTraversal(T, root)
 *     order ê empty list
 *     ForAll child nodes c of root
 *         order ê order + PostOrderTraversal(T, c)
 *     EndFor
 *     Append root to order
 *     Return order
 * EndFunction
 */
std::vector<int> post_order_traversal(const std::vector<std::vector<int>>& tree_structure,
                                      int root);

/**
 * Algorithm 4.7: Compute Local Multiplicities
 * 
 * DETAILED FLOW:
 * "For each parent-child pair (v, c), the algorithm invokes ComputeLocalMultiplicities
 * with tables R_v (target) and R_c (source), along with constraint parameters that encode
 * the join condition. This updates each tuple t_v  R_v by computing:
 * t_v.local_mult^{new} = t_v.local_mult^{old} ◊ £{t_c  R_c : (t_v, t_c) satisfy constraint(v,c)} t_c.local_mult"
 * 
 * PSEUDOCODE:
 * Function ComputeLocalMultiplicities(R_target, R_source, constraint_param)
 *     R_comb ê CombineTable(R_target, R_source, constraint_param)
 *     R_comb ê Map(R_comb, ªe: (e.local_cumsum ê e.local_mult, e.local_interval ê 0, e))
 *     ObliviousSort(R_comb, ComparatorJoinAttr)
 *     LinearPass(R_comb, WindowComputeLocalSum)
 *     ObliviousSort(R_comb, ComparatorPairwise)
 *     LinearPass(R_comb, WindowComputeLocalInterval)
 *     ObliviousSort(R_comb, ComparatorEndFirst)
 *     R_truncated ê R_comb[1:|R_target|]
 *     ParallelPass(R_truncated, R_target, UpdateTargetMultiplicity)
 *     Return R_target
 * EndFunction
 * 
 * STEPS EXPLAINED:
 * 1. CombineTable: Create START/END boundaries for target tuples, merge with source
 * 2. Map: Initialize local_cumsum with local_mult values
 * 3. Sort by join attribute: Groups entries by value with precedence ordering
 * 4. WindowComputeLocalSum: Accumulate multiplicities via cumulative sum
 * 5. Sort pairwise: Place START/END pairs adjacent
 * 6. WindowComputeLocalInterval: Compute difference between START/END cumsum
 * 7. Sort END first: Bring END entries (with intervals) to front
 * 8. Extract and align: Take first |R_target| entries
 * 9. ParallelPass: Update target multiplicities with computed intervals
 */
void compute_local_multiplicities(Table& target_table, 
                                 const Table& source_table,
                                 const JoinCondition& join_condition);

/**
 * Algorithm 4.8: Combine Table (Dual-Entry Creation)
 * 
 * DUAL-ENTRY TRANSFORMATION:
 * "For a target tuple with join attribute value v, the boundary parameters create:
 * (i) a START entry at v + deviation1 where if equality1 = EQ, it includes values e v + deviation1,
 *     and if equality1 = NEQ, it includes values > v + deviation1
 * (ii) an END entry at v + deviation2 where if equality2 = EQ, it includes values d v + deviation2,
 *      and if equality2 = NEQ, it includes values < v + deviation2"
 * 
 * PSEUDOCODE:
 * Function CombineTable(R_target, R_source, constraint_param)
 *     ((deviation1, equality1), (deviation2, equality2)) ê constraint_param
 *     
 *     R_source' ê Map(R_source, function(t):
 *         e.field_type ê SOURCE
 *         e.field_equality_type ê null
 *         e.join_attr ê t.join_attr
 *         // ... copy other fields
 *         Return e
 *     
 *     R_begin' ê Map(R_target, function(t):
 *         e.field_type ê START
 *         e.field_equality_type ê equality1
 *         e.join_attr ê t.join_attr + deviation1
 *         // ... copy other fields
 *         Return e
 *     
 *     R_end' ê Map(R_target, function(t):
 *         e.field_type ê END
 *         e.field_equality_type ê equality2
 *         e.join_attr ê t.join_attr + deviation2
 *         // ... copy other fields
 *         Return e
 *     
 *     R_comb ê R_source' + R_begin' + R_end'
 *     Return R_comb
 * EndFunction
 * 
 * KEY TRANSFORMATION:
 * "This dual entry approach transforms a complex range matching problem into a simple
 * interval computation. The key insight is that start and end entries define interval
 * boundaries in the sorted combined table, and the cumulative counter tracks all relevant
 * contributions seen so far."
 */
Table combine_table(const Table& target_table,
                   const Table& source_table,
                   const JoinCondition& join_condition);

#endif // PHASE1_BOTTOM_UP_H