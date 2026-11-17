#ifndef ALIGN_CONCAT_H
#define ALIGN_CONCAT_H

#include <memory>
#include <vector>
#include "../data_structures/data_structures.h"
#include "../join/join_tree_node.h"

/**
 * AlignConcat Class
 * 
 * Implements Phase 4 of the oblivious multi-way band join algorithm.
 * Constructs the final join result by aligning expanded tables and concatenating them.
 * 
 * From the thesis (Section 4.5):
 * "After expansion, tables must be aligned so that matching tuples appear in the same rows.
 * The parent table is sorted by join attributes (and secondarily by other attributes),
 * creating groups of identical tuples. Each group represents a distinct combination from
 * the parent table that will be matched with corresponding child tuples."
 * 
 * The alignment key formula: foreign_sum + (copy_index / local_mult)
 * ensures that every local_mult copies of a child tuple increment to the next parent group,
 * correctly distributing child copies across matching parent groups.
 */
class AlignConcat {
public:
    /**
     * Execute align-concat phase on the join tree
     * Constructs the final join result by aligning and concatenating tables
     *
     * @param root Root of the join tree with expanded tables
     * @return Final join result table
     */
    static Table Execute(JoinTreeNodePtr root);
    
    /**
     * Get sorting metrics collected during align-concat phase
     *
     * @param total_time Total time spent sorting (output)
     * @param acc_time Time spent sorting accumulators (output)
     * @param child_time Time spent sorting children (output)
     */
    static void GetSortingMetrics(double& total_time,
                                  double& acc_time,
                                  double& child_time);
    
    /**
     * Reset sorting metrics to zero
     * Should be called before each join execution
     */
    static void ResetSortingMetrics();

private:
    /**
     * Recursively construct join result through the tree
     * Processes tree in pre-order, accumulating results
     *
     * @param root Current root of subtree
     * @return Accumulated join result for this subtree
     */
    static Table ConstructJoinResult(JoinTreeNodePtr root);
    
    /**
     * Align child table with accumulator and concatenate
     * Core operation that aligns two tables and merges them horizontally
     *
     * Algorithm steps:
     * 1. Sort accumulator by join attr, then other attrs
     * 2. Compute copy indices for child via linear pass
     * 3. Compute alignment keys for child
     * 4. Sort child by alignment key
     * 5. Horizontal concatenation
     *
     * @param accumulator Current accumulated result (parent)
     * @param child Child table to align and add
     * @param child_rows_regrouped True if the child is the result of a node
     *        with two or more children: its rows were re-sorted once per
     *        child, so steps 2-3 use ComputeAlignmentKeysByRank instead
     * @return Concatenated result table
     */
    static Table AlignAndConcatenate(const Table& accumulator,
                                     const Table& child,
                                     bool child_rows_regrouped);
    
    /**
     * Pre-order traversal of join tree
     * Visits parent before children for result construction
     * 
     * @param root Root of subtree to traverse
     * @return Nodes in pre-order sequence
     */
    static std::vector<JoinTreeNodePtr> PreOrderTraversal(JoinTreeNodePtr root);
    
    /**
     * Compute copy indices for an expanded table
     * Sets copy_index from 0 to (final_mult-1) for each original tuple
     *
     * Uses linear pass to track:
     * - Same original_index -> increment copy_index
     * - Different original_index -> reset to 0
     *
     * @param table Expanded table to process
     * @return Table with copy indices set
     */
    static Table ComputeCopyIndices(const Table& table);

    /**
     * Compute alignment keys for child table
     * Uses formula: foreign_sum + (copy_index / local_mult)
     *
     * @param table Table with copy indices set
     * @return Table with alignment keys computed
     */
    static Table ComputeAlignmentKeys(const Table& table);

    /**
     * Compute alignment keys for a child whose rows are not arranged in
     * repeating blocks.
     *
     * The copy-index formula above relies on the copies of one child tuple
     * cycling through that tuple's sub-results (its combinations with its own
     * subtree) every local_mult rows. That holds for a leaf and for a node
     * with one child. A node with two or more children is re-sorted by
     * (join key, all attributes) for each child after the first, which groups
     * equal sub-results together; copy_index / local_mult would then hand one
     * parent group several copies of the same sub-result and none of another.
     *
     * Instead: sort so identical rows are adjacent, rank each row among its
     * identical rows, and use
     *   alignment_key = foreign_sum + rank mod (final_mult / local_mult).
     * A tuple is spread over final_mult / local_mult parent groups and each of
     * its sub-results occurs a multiple of that many times, so every parent
     * group receives each sub-result exactly as often as it should - whatever
     * order the rows arrive in, and also when tuples are duplicated.
     *
     * Costs one extra oblivious sort of the child table. Whether it runs
     * depends only on the shape of the join tree.
     *
     * @param table Child result with its join key set
     * @param sort_time Receives the time spent in the extra sort (seconds)
     * @return Table with alignment keys computed
     */
    static Table ComputeAlignmentKeysByRank(const Table& table, double& sort_time);
};

#endif // ALIGN_CONCAT_H