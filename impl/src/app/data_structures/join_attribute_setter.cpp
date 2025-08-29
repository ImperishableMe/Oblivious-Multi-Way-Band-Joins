#include "join_attribute_setter.h"
#include "../Enclave_u.h"
#include <stdexcept>
#include <sstream>

void JoinAttributeSetter::SetJoinAttributesForTree(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    DEBUG_DEBUG("Setting join attributes for tree rooted at %s", root->get_table_name().c_str());
    
    // Process current node
    SetJoinAttributesForNode(root, eid);
    
    // Recursively process children
    for (auto& child : root->get_children()) {
        SetJoinAttributesForTree(child, eid);
    }
}

void JoinAttributeSetter::SetJoinAttributesForNode(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
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
    
    // Get column index from first entry
    if (table.size() == 0) {
        DEBUG_WARN("Table %s is empty, cannot set join attributes", node->get_table_name().c_str());
        return;
    }
    
    int32_t column_index = GetColumnIndex(table[0], join_column);
    if (column_index < 0) {
        DEBUG_ERROR("Column %s not found in table %s", 
                    join_column.c_str(), node->get_table_name().c_str());
        return;
    }
    
    DEBUG_INFO("Column %s is at index %d", join_column.c_str(), column_index);
    
    // Use ecall to set join_attr for each entry
    for (size_t i = 0; i < table.size(); i++) {
        Entry& entry = table[i];
        entry_t c_entry = entry.to_entry_t();
        
        // Debug: Print join_attr before
        DEBUG_DEBUG("Entry %zu before: join_attr=%d, is_encrypted=%d", 
                    i, c_entry.join_attr, c_entry.is_encrypted);
        
        // Call ecall to set join_attr (handles decryption/encryption)
        sgx_status_t status = ecall_transform_set_join_attr(eid, &c_entry, column_index);
        
        if (status != SGX_SUCCESS) {
            DEBUG_ERROR("Failed to set join_attr for entry %zu: SGX error 0x%x", i, status);
            continue;
        }
        
        // Update entry from c_entry
        entry.from_entry_t(c_entry);
        
        // Debug: Print join_attr after
        DEBUG_DEBUG("Entry %zu after: join_attr=%d from column %s (index %d)",
                   i, entry.join_attr, join_column.c_str(), column_index);
        
        // Additional debug: Show all attributes to verify
        if (i == 0) {
            DEBUG_INFO("First entry attributes for verification:");
            for (size_t j = 0; j < entry.attributes.size(); j++) {
                DEBUG_INFO("  attr[%zu]=%d (column: %s)", j, entry.attributes[j], 
                          j < entry.column_names.size() ? entry.column_names[j].c_str() : "unknown");
            }
        }
    }
}

int32_t JoinAttributeSetter::GetColumnIndex(const Entry& entry, const std::string& column_name) {
    // Search through column names to find index
    for (size_t i = 0; i < entry.column_names.size(); i++) {
        if (entry.column_names[i] == column_name) {
            return static_cast<int32_t>(i);
        }
    }
    
    // Not found
    return -1;
}

void JoinAttributeSetter::SetJoinAttributesForTable(Table& table, const std::string& column_name, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        DEBUG_WARN("Table is empty, cannot set join attributes");
        return;
    }
    
    // Get column index from first entry
    int32_t column_index = GetColumnIndex(table[0], column_name);
    if (column_index < 0) {
        DEBUG_ERROR("Column %s not found in table", column_name.c_str());
        return;
    }
    
    DEBUG_INFO("Setting join_attr for %zu entries using column %s (index %d)",
               table.size(), column_name.c_str(), column_index);
    
    // Use ecall to set join_attr for each entry
    for (size_t i = 0; i < table.size(); i++) {
        Entry& entry = table[i];
        entry_t c_entry = entry.to_entry_t();
        
        // Call ecall to set join_attr (handles decryption/encryption)
        sgx_status_t status = ecall_transform_set_join_attr(eid, &c_entry, column_index);
        
        if (status != SGX_SUCCESS) {
            DEBUG_ERROR("Failed to set join_attr for entry %zu: SGX error 0x%x", i, status);
            continue;
        }
        
        // Update entry from c_entry
        entry.from_entry_t(c_entry);
    }
}