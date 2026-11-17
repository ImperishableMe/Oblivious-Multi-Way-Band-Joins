#include "join_attribute_setter.h"
#include "op_types.h"
#include <stdexcept>
#include <sstream>

void JoinAttributeSetter::SetJoinAttributesForTree(JoinTreeNodePtr root) {
    DEBUG_DEBUG("Setting join attributes for tree rooted at %s", root->get_table_name().c_str());

    // Process current node
    SetJoinAttributesForNode(root);

    // Recursively process children
    for (auto& child : root->get_children()) {
        SetJoinAttributesForTree(child);
    }
}

void JoinAttributeSetter::SetJoinAttributesForNode(JoinTreeNodePtr node) {
    std::string join_column = node->get_join_column();

    // Root node might not have a join column set
    if (join_column.empty() && !node->is_root()) {
        DEBUG_WARN("Node %s has no join column set", node->get_table_name().c_str());
        return;
    }

    // For root, we might need to set join column from first constraint
    if (join_column.empty() && node->is_root() && !node->get_children().empty()) {
        // Get the first child's constraint
        const JoinConstraint& child_constraint = node->get_children()[0]->get_constraint_with_parent();
        join_column = child_constraint.get_target_column();
        node->set_join_column(join_column);
        DEBUG_INFO("Set root node %s join column to %s",
                   node->get_table_name().c_str(), join_column.c_str());
    }

    // If still no join column, skip
    if (join_column.empty()) {
        DEBUG_DEBUG("Node %s has no join column, skipping", node->get_table_name().c_str());
        return;
    }

    // Get the table and update each entry
    Table& table = node->get_table();
    DEBUG_INFO("Setting join_attr for %zu entries in %s using column %s",
               table.size(), node->get_table_name().c_str(), join_column.c_str());

    // Single-column key: no equality components
    ApplyJoinKey(table, node->get_table_name(), join_column,
                 std::vector<std::string>(), std::vector<int32_t>());
}

void JoinAttributeSetter::SetSourceJoinKey(Table& table, const JoinConstraint& constraint) {
    DEBUG_INFO("Setting source join key for %zu entries of %s: range column %s, %zu equality column(s)",
               table.size(), constraint.get_source_table().c_str(),
               constraint.get_source_column().c_str(), constraint.num_equalities());

    // The source side carries its columns as they are; all constant offsets
    // are applied on the target side.
    std::vector<int32_t> no_offsets(constraint.num_equalities(), 0);
    ApplyJoinKey(table, constraint.get_source_table(), constraint.get_source_column(),
                 constraint.get_source_eq_columns(), no_offsets);
}

void JoinAttributeSetter::SetTargetJoinKey(Table& table, const JoinConstraint& constraint) {
    DEBUG_INFO("Setting target join key for %zu entries of %s: range column %s, %zu equality column(s)",
               table.size(), constraint.get_target_table().c_str(),
               constraint.get_target_column().c_str(), constraint.num_equalities());

    // source.eq = target.eq + offset, so the target's key component is
    // target.eq + offset. The range column gets its deviations later, when the
    // START/END boundary entries are created.
    ApplyJoinKey(table, constraint.get_target_table(), constraint.get_target_column(),
                 constraint.get_target_eq_columns(), constraint.get_eq_offsets());
}

int32_t JoinAttributeSetter::ResolveColumn(const Table& table,
                                           const std::string& table_alias,
                                           const std::string& column_name) {
    // A node's own table uses bare column names. Once it has been
    // concatenated with a child in the align-concat phase, its columns are
    // named "alias.column".
    const std::string qualified_name = table_alias + "." + column_name;
    const std::vector<std::string> schema = table.get_schema();

    for (size_t i = 0; i < schema.size(); i++) {
        if (schema[i] == column_name || schema[i] == qualified_name) {
            if (i >= MAX_ATTRIBUTES) {
                throw std::runtime_error("Join column '" + column_name + "' of table '" + table_alias +
                                         "' lies beyond MAX_ATTRIBUTES=" + std::to_string(MAX_ATTRIBUTES));
            }
            return static_cast<int32_t>(i);
        }
    }

    throw std::runtime_error("Join column '" + column_name + "' not found in table '" + table_alias + "'");
}

void JoinAttributeSetter::ApplyJoinKey(Table& table,
                                       const std::string& table_alias,
                                       const std::string& range_column,
                                       const std::vector<std::string>& eq_columns,
                                       const std::vector<int32_t>& eq_offsets) {
    if (eq_columns.size() > MAX_JOIN_EQ || eq_columns.size() != eq_offsets.size()) {
        throw std::runtime_error("Invalid join key for table '" + table_alias + "': " +
                                 std::to_string(eq_columns.size()) + " equality column(s), at most " +
                                 std::to_string(MAX_JOIN_EQ) + " supported");
    }

    // Describe the key by column index (JOIN_KEY_PARAM_* layout, constants.h).
    // Everything in it comes from the query and the table schema.
    int32_t params[JOIN_KEY_PARAM_COUNT] = {0};
    params[JOIN_KEY_PARAM_RANGE_COLUMN] = ResolveColumn(table, table_alias, range_column);
    params[JOIN_KEY_PARAM_NUM_EQ] = static_cast<int32_t>(eq_columns.size());
    for (size_t i = 0; i < eq_columns.size(); i++) {
        params[JOIN_KEY_PARAM_EQ_COLUMNS + i] = ResolveColumn(table, table_alias, eq_columns[i]);
        params[JOIN_KEY_PARAM_EQ_OFFSETS + i] = eq_offsets[i];
    }

    if (table.size() == 0) {
        DEBUG_WARN("Table is empty, cannot set join attributes");
        return;
    }

    // Use batched operation to set the join key for all entries
    Table updated = table.map(OP_ECALL_TRANSFORM_SET_JOIN_ATTR, params);

    // Replace the table's entries with the updated ones
    table = updated;
}
