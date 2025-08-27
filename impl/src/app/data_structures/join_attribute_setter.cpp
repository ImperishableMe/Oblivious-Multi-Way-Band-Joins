#include "join_attribute_setter.h"
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
    
    for (size_t i = 0; i < table.size(); i++) {
        Entry& entry = table[i];
        
        try {
            double value = ExtractColumnValue(entry, join_column);
            entry.join_attr = static_cast<int32_t>(value);
            
            DEBUG_TRACE("Entry %zu: Set join_attr to %d from column %s",
                       i, entry.join_attr, join_column.c_str());
        } catch (const std::exception& e) {
            DEBUG_ERROR("Failed to extract column %s from entry %zu: %s",
                       join_column.c_str(), i, e.what());
            // Set a default value or throw depending on requirements
            entry.join_attr = 0.0;
        }
    }
}

double JoinAttributeSetter::ExtractColumnValue(const Entry& entry, const std::string& column_name) {
    // Check if column exists
    if (!entry.has_column(column_name)) {
        throw std::runtime_error("Column not found: " + column_name);
    }
    
    // Get the attribute value directly by column name
    int32_t value = entry.get_attribute(column_name);
    
    // Convert to double
    return static_cast<double>(value);
}