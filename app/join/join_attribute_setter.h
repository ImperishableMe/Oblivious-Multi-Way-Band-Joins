#ifndef JOIN_ATTRIBUTE_SETTER_H
#define JOIN_ATTRIBUTE_SETTER_H

#include <memory>
#include <string>
#include <vector>
#include "join_tree_node.h"
#include "join_constraint.h"
#include "../../common/debug_util.h"

/**
 * JoinAttributeSetter - Sets the join key of all entries of a table
 *
 * After the join tree is built, each edge knows which columns it joins on,
 * but the Entry objects don't have their join key set. This utility copies
 * the join columns of one side of a constraint into the entries' join key
 * (join_eq for the equality columns, join_attr for the range column).
 */
class JoinAttributeSetter {
public:
    /**
     * Set join attributes for entire tree (DEPRECATED - use SetSourceJoinKey /
     * SetTargetJoinKey per constraint)
     * @param root The root of the join tree
     */
    static void SetJoinAttributesForTree(JoinTreeNodePtr root);

    /**
     * Set the join key of the source (child) side of a constraint.
     *
     * @param table      Rows of the constraint's source table: either that
     *                   table itself, or a result it has already been
     *                   concatenated into (whose columns are named
     *                   "alias.column").
     * @param constraint The constraint between the child and its parent
     * @throws std::runtime_error if a join column is not in the table
     */
    static void SetSourceJoinKey(Table& table, const JoinConstraint& constraint);

    /**
     * Set the join key of the target (parent) side of a constraint.
     * Constant offsets of the constraint's equalities are folded into the
     * parent's key, so equal keys on both sides mean the equalities hold.
     *
     * @param table      Rows of the constraint's target table (see above)
     * @param constraint The constraint between the child and its parent
     * @throws std::runtime_error if a join column is not in the table
     */
    static void SetTargetJoinKey(Table& table, const JoinConstraint& constraint);

private:
    /**
     * Set join attributes for a single node
     * @param node The node to process
     */
    static void SetJoinAttributesForNode(JoinTreeNodePtr node);

    /**
     * Find a column of table `table_alias` in `table`, by its bare name or,
     * for concatenated results, by its "alias.column" name.
     * @throws std::runtime_error if the column does not exist
     */
    static int32_t ResolveColumn(const Table& table,
                                 const std::string& table_alias,
                                 const std::string& column_name);

    /**
     * Copy the given columns into the join key of every entry:
     * range_column into join_attr, eq_columns[i] + eq_offsets[i] into join_eq[i].
     */
    static void ApplyJoinKey(Table& table,
                             const std::string& table_alias,
                             const std::string& range_column,
                             const std::vector<std::string>& eq_columns,
                             const std::vector<int32_t>& eq_offsets);
};

#endif // JOIN_ATTRIBUTE_SETTER_H
