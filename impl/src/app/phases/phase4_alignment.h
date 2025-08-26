#ifndef PHASE4_ALIGNMENT_H
#define PHASE4_ALIGNMENT_H

#include <vector>
#include "../types.h"
#include "../../enclave/enclave_types.h"

/**
 * PHASE 4: ALIGNMENT AND CONCATENATION
 * =====================================
 * 
 * From thesis section 4.6:
 * "After expansion, tables must be aligned so that matching tuples appear in the same rows.
 * The parent table is sorted by join attributes (and secondarily by other attributes for
 * deterministic ordering), creating groups of identical tuples. Each group represents a
 * distinct combination from the parent table that will be matched with corresponding child tuples."
 * 
 * ALIGNMENT FORMULA:
 * "The child table alignment uses the formula foreign_sum + (copy_index ˜ local_mult), where:
 * - foreign_sum is the index of the first parent group that matches this child tuple
 * - copy_index is the index of this copy among all copies of the same original tuple (0 to final_mult-1)
 * - local_mult is the child tuple's local multiplicity
 * 
 * This formula ensures that every local_mult copies of a child tuple increment to the next
 * parent group, correctly distributing child copies across matching parent groups."
 * 
 * KEY INSIGHT:
 * "After sorting by this alignment key, corresponding rows from parent and child tables
 * are horizontally concatenated to form the partial join result. This process continues
 * recursively through the join tree until all tables are combined."
 */

/**
 * Algorithm 4.18: Result Construction
 * 
 * PSEUDOCODE:
 * Function ConstructJoinResult(T, root)
 *     result ê ObliviousExpand(R_root)  // Expand root table
 *     ForAll nodes v in pre-order traversal of T from root
 *         ForAll child nodes c of v
 *             R_c^expanded ê ObliviousExpand(R_c)  // Expand child table
 *             result ê AlignAndConcatenate(result, R_c^expanded)
 *         EndFor
 *     EndFor
 *     Return result
 * EndFunction
 * 
 * PROCESS:
 * 1. Start with expanded root table as initial result
 * 2. For each parent-child pair in pre-order:
 *    - Expand child table
 *    - Align child with accumulated result
 *    - Concatenate horizontally
 * 3. Final result contains all tables joined correctly
 */
Table construct_join_result(const std::vector<Table>& expanded_tables,
                           const std::vector<std::vector<int>>& tree_structure,
                           int root);

/**
 * Algorithm 4.19: Align and Concatenate
 * 
 * PSEUDOCODE:
 * Function AlignAndConcatenate(R_accumulator, R_child)
 *     ObliviousSort(R_accumulator, JoinThenOtherAttributes)  // Sort by join attrs, then others
 *     LinearPass(R_child, ComputeAlignmentKey)  // Set alignment key for each tuple
 *     ObliviousSort(R_child, AlignmentKeyComparator)
 *     Return HorizontalConcatenate(R_accumulator, R_child)
 * EndFunction
 * 
 * STEPS:
 * 1. Sort parent/accumulator by join attribute (creating groups)
 * 2. Compute alignment key for each child tuple
 * 3. Sort child by alignment key
 * 4. Horizontally concatenate aligned tables
 * 
 * After alignment, row i in parent matches row i in child
 */
Table align_and_concatenate(Table& accumulator, Table& child);

/**
 * Algorithm 4.20: Compute Alignment Key
 * 
 * PSEUDOCODE:
 * Function ComputeAlignmentKey(tuple)
 *     tuple.alignment_key ê tuple.foreign_sum + (tuple.copy_index ˜ tuple.local_mult)
 * EndFunction
 * 
 * FORMULA EXPLANATION:
 * - foreign_sum: Index of first matching parent group
 * - copy_index: Which copy of this tuple (0 to final_mult-1)
 * - local_mult: How many copies before moving to next parent group
 * 
 * Example: If a child tuple has:
 * - foreign_sum = 10 (starts matching at parent group 10)
 * - local_mult = 3 (3 copies per parent group)
 * - final_mult = 9 (total 9 copies)
 * 
 * Then its copies have alignment_keys:
 * - copy 0,1,2: alignment_key = 10 + (0,1,2)˜3 = 10
 * - copy 3,4,5: alignment_key = 10 + (3,4,5)˜3 = 11
 * - copy 6,7,8: alignment_key = 10 + (6,7,8)˜3 = 12
 * 
 * This distributes the 9 copies across 3 parent groups (10, 11, 12)
 */
void compute_alignment_keys(Table& table);

/**
 * Helper: Horizontal concatenation
 * Combines all columns from both tables while maintaining same number of rows
 * Each row in result contains attributes from corresponding rows in both input tables
 * 
 * Prerequisites:
 * - Both tables must have same number of rows
 * - Tables must be properly aligned (matching tuples in same row positions)
 * 
 * Result schema: [accumulator_columns | child_columns]
 */
Table horizontal_concatenate(const Table& accumulator, const Table& child);

/**
 * Helper: Sort parent table deterministically
 * Primary sort: join attribute
 * Secondary sort: original_index (for deterministic groups)
 * 
 * This creates groups where all tuples with same join attribute
 * are consecutive, with consistent ordering within groups
 */
void sort_parent_table(Table& parent);

/**
 * Helper: Sort child table by alignment key
 * After this sort, child tuples are positioned to match
 * their corresponding parent groups
 */
void sort_child_by_alignment(Table& child);

/**
 * COMPLEXITY ANALYSIS:
 * 
 * For each parent-child pair:
 * - Sorting parent: O(|R_parent| log |R_parent|)
 * - Computing alignment keys: O(|R_child|)
 * - Sorting child: O(|R_child| log |R_child|)
 * - Concatenation: O(|R_parent| + |R_child|)
 * 
 * Total phase: O(|Result| log |Result|) where |Result| is final join size
 * 
 * CORRECTNESS:
 * The alignment formula guarantees that:
 * 1. Each child tuple matches exactly the parent tuples it should join with
 * 2. The distribution respects the multiplicities computed in earlier phases
 * 3. The final result contains exactly the correct join output
 */

#endif // PHASE4_ALIGNMENT_H