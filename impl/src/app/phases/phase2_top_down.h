#ifndef PHASE2_TOP_DOWN_H
#define PHASE2_TOP_DOWN_H

#include <vector>
#include "../types.h"
#include "../../enclave/enclave_types.h"

/**
 * PHASE 2: TOP-DOWN FINAL MULTIPLICITY PROPAGATION
 * ==================================================
 * 
 * From thesis section 4.4:
 * "The top-down phase propagates final multiplicities (±_final) from the root to all
 * nodes in the tree, mirroring the reconstruction phase of Yannakakis. This phase
 * computes how many times each tuple appears in the complete join result by considering
 * contributions from outside its subtree."
 * 
 * KEY RELATIONSHIP:
 * "The key insight is that each child tuple's final multiplicity equals its local
 * multiplicity times its foreign multiplicity, where the foreign multiplicity (±_foreign)
 * represents the number of join results from tables outside the child's subtree that
 * connect through the parent. This is computed as: t_c.final_mult = t_c.local_mult ◊ t_c.foreign_mult"
 * 
 * FORMAL DEFINITIONS:
 * 
 * "Local Multiplicity (±_local): For a tuple t in table R_v at node v in the join tree,
 * the local multiplicity represents the number of times t participates in the join result
 * when considering only the visited portion of the subtree rooted at v."
 * 
 * "Final Multiplicity (±_final): For any tuple t in any table, the final multiplicity
 * represents the number of times t appears in the complete join result across all tables.
 * For the root node, ±_final = ±_local."
 * 
 * "Foreign Multiplicity (±_foreign): For a tuple t in table R_v at node v in the join tree,
 * the foreign multiplicity represents the number of times t participates in the join result
 * when considering all tables OUTSIDE the subtree rooted at v, plus the node v itself."
 */

/**
 * Algorithm 4.14: Top-Down Phase
 * 
 * PSEUDOCODE:
 * Function TopDownPhase(T, root)
 *     ForAll tuple t  R_root
 *         t.final_mult ê t.local_mult  // Root final = local
 *     EndFor
 *     ForAll nodes v in pre-order traversal of T from root
 *         ForAll child nodes c of v
 *             R_c ê PropagateFinalMultiplicities(R_v, R_c, constraint(v,c))
 *         EndFor
 *     EndFor
 *     Return T  // Return tree with tables containing computed final multiplicities
 * EndFunction
 * 
 * TRAVERSAL ORDER:
 * "The traversal proceeds in pre-order, starting from the root where final_mult = local_mult
 * (since the root has no ancestors), then propagating downward to compute each child's
 * final multiplicity based on its parent's values."
 * 
 * COMPLEXITY: O(£_{(v,c)  E} (|R_v| + |R_c|) log≤(|R_v| + |R_c|))
 */
void top_down_phase(std::vector<Table>& tables,
                    const std::vector<JoinCondition>& join_conditions,
                    const std::vector<std::vector<int>>& tree_structure,
                    int root);

/**
 * Pre-order traversal helper
 * Visits parent before children
 */
std::vector<int> pre_order_traversal(const std::vector<std::vector<int>>& tree_structure,
                                     int root);

/**
 * Algorithm 4.15: Propagate Final Multiplicities
 * 
 * CORE QUESTION:
 * "The core question in the top-down phase is: what would be the multiplicity of each
 * parent tuple if we excluded the child table and its entire subtree? That is, what is
 * the multiplicity of parent (source) table entries in the join result of T \\ T_c?"
 * 
 * KEY TECHNIQUE:
 * "We use a running sum called 'local weight' to track the sum of matching child tuples'
 * local multiplicities---this represents the child subtree's contribution. By dividing
 * a parent tuple's final multiplicity by this local weight, we recover its multiplicity
 * in T \\ T_c. The sum of these multiplicities for all matching parent tuples gives us
 * the foreign multiplicity."
 * 
 * PSEUDOCODE:
 * Function PropagateFinalMultiplicities(R_source, R_target, constraint_param)
 *     R_comb ê CombineTable(R_target, R_source, constraint_param)
 *     R_comb ê Map(R_comb, ªe:
 *         (e.local_weight ê e.local_mult,
 *          e.foreign_cumsum ê 0,
 *          e.foreign_interval ê 0, e))
 *     ObliviousSort(R_comb, ComparatorJoinAttr)
 *     LinearPass(R_comb, WindowComputeForeignSum)
 *     ObliviousSort(R_comb, ComparatorPairwise)
 *     LinearPass(R_comb, WindowComputeForeignInterval)
 *     ObliviousSort(R_comb, ComparatorEndFirst)
 *     R_truncated ê R_comb[1:|R_target|]
 *     ParallelPass(R_truncated, R_target, UpdateTargetFinalMultiplicity)
 *     Return R_target
 * EndFunction
 * 
 * DUAL COUNTER TECHNIQUE:
 * "We simultaneously track two counters:
 * 1. LOCAL WEIGHT: Sum of local multiplicities of matching children
 * 2. FOREIGN CUMSUM: Accumulated foreign contributions
 * 
 * The division final_mult/local_weight recovers parent's multiplicity in T \\ T_c"
 */
void propagate_final_multiplicities(const Table& source_table,
                                   Table& target_table,
                                   const JoinCondition& join_condition);

/**
 * FOREIGN SUM AND ALIGNMENT:
 * 
 * "Foreign Multiplicity Sum (foreign_sum): For a child tuple t_c in table R_c with parent
 * node v, if we were to join all tables in T \\ T_c^- and sort the result by the join
 * attribute between v and c, then t_c.foreign_sum is the index of the first entry from
 * the parent table that matches t_c."
 * 
 * "This foreign_sum serves dual purposes:
 * 1. It provides the foreign multiplicity for computing final_mult = local_mult ◊ foreign_mult
 * 2. Later serves as the alignment key during result construction (Phase 4)"
 */

#endif // PHASE2_TOP_DOWN_H