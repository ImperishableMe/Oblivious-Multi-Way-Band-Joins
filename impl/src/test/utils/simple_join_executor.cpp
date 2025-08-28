#include "simple_join_executor.h"
#include <iostream>
#include <algorithm>

Table SimpleJoinExecutor::execute_join_tree(JoinTreeNodePtr root) {
    if (!root) {
        throw std::runtime_error("Cannot execute null join tree");
    }
    
    return join_subtree(root);
}

Table SimpleJoinExecutor::join_subtree(JoinTreeNodePtr node) {
    // Base case: leaf node returns its table
    if (node->is_leaf()) {
        Table result = node->get_table();
        
        // Decrypt if needed
        if (should_decrypt) {
            for (size_t i = 0; i < result.size(); i++) {
                Entry decrypted = decrypt_if_needed(result[i]);
                result.set_entry(i, decrypted);
            }
        }
        
        return result;
    }
    
    // Start with current node's table
    Table result = node->get_table();
    
    // Decrypt current table if needed
    if (should_decrypt) {
        for (size_t i = 0; i < result.size(); i++) {
            Entry decrypted = decrypt_if_needed(result[i]);
            result.set_entry(i, decrypted);
        }
    }
    
    // Join with each child subtree
    for (const auto& child : node->get_children()) {
        // Recursively get child subtree result
        Table child_result = join_subtree(child);
        
        // Get constraint from child to parent
        JoinConstraint constraint = child->get_constraint_with_parent();
        
        // Join current result with child result
        result = join_tables(result, child_result, constraint);
    }
    
    return result;
}

Table SimpleJoinExecutor::join_tables(
    const Table& left,
    const Table& right,
    const JoinConstraint& constraint) {
    
    Table result("joined");
    
    // Get column names for the join
    std::string left_col = constraint.get_target_column();  // Parent column
    std::string right_col = constraint.get_source_column(); // Child column
    
    // Nested loop join
    for (size_t i = 0; i < left.size(); i++) {
        for (size_t j = 0; j < right.size(); j++) {
            Entry left_entry = left[i];
            Entry right_entry = right[j];
            
            // Check if entries satisfy the join constraint
            if (satisfies_constraint(left_entry, right_entry, constraint, left_col, right_col)) {
                // Concatenate entries and add to result
                Entry joined = concatenate_entries(
                    left_entry, right_entry,
                    constraint.get_target_table(),
                    constraint.get_source_table()
                );
                result.add_entry(joined);
            }
        }
    }
    
    return result;
}

bool SimpleJoinExecutor::satisfies_constraint(
    const Entry& left,
    const Entry& right,
    const JoinConstraint& constraint,
    const std::string& left_col,
    const std::string& right_col) {
    
    // Get column values
    int32_t left_value = get_column_value(left, left_col);
    int32_t right_value = get_column_value(right, right_col);
    
    // Get constraint bounds
    int32_t lower_dev = constraint.get_deviation1();
    int32_t upper_dev = constraint.get_deviation2();
    equality_type_t lower_eq = constraint.get_equality1();
    equality_type_t upper_eq = constraint.get_equality2();
    
    // The constraint is: right_value IN [left_value + lower_dev, left_value + upper_dev]
    
    // Check lower bound
    if (lower_dev != JOIN_ATTR_NEG_INF) {
        int32_t lower_bound = left_value + lower_dev;
        if (lower_eq == EQ) {
            if (right_value < lower_bound) return false;
        } else { // NEQ (open interval)
            if (right_value <= lower_bound) return false;
        }
    }
    
    // Check upper bound
    if (upper_dev != JOIN_ATTR_POS_INF) {
        int32_t upper_bound = left_value + upper_dev;
        if (upper_eq == EQ) {
            if (right_value > upper_bound) return false;
        } else { // NEQ (open interval)
            if (right_value >= upper_bound) return false;
        }
    }
    
    return true;
}

Entry SimpleJoinExecutor::concatenate_entries(
    const Entry& left,
    const Entry& right,
    const std::string& left_table_name,
    const std::string& right_table_name) {
    
    Entry result;
    
    // Copy ALL attributes from left entry (preserving original names)
    auto left_attrs = left.get_attributes_map();
    for (const auto& [col_name, value] : left_attrs) {
        // Just use the column name as-is (SQLite style)
        result.add_attribute(col_name, value);
    }
    
    // Copy ALL attributes from right entry (preserving original names)
    // All TPC-H column names are unique, so no duplicates to worry about
    auto right_attrs = right.get_attributes_map();
    for (const auto& [col_name, value] : right_attrs) {
        result.add_attribute(col_name, value);
    }
    
    // Set other properties
    result.is_encrypted = false;  // Result is decrypted
    
    return result;
}

int32_t SimpleJoinExecutor::get_column_value(const Entry& entry, const std::string& column_name) {
    // Try exact match first
    if (entry.has_attribute(column_name)) {
        return entry.get_attribute(column_name);
    }
    
    // Try with any table prefix (e.g., "table.column")
    auto attrs = entry.get_attributes_map();
    for (const auto& [name, value] : attrs) {
        size_t dot_pos = name.find('.');
        if (dot_pos != std::string::npos) {
            std::string col_part = name.substr(dot_pos + 1);
            if (col_part == column_name) {
                return value;
            }
        }
    }
    
    // Column not found, return 0
    return 0;
}

Entry SimpleJoinExecutor::decrypt_if_needed(const Entry& entry) {
    if (!entry.is_encrypted) {
        return entry;  // Already decrypted
    }
    
    Entry decrypted = entry;
    
    // Use real decryption if enclave is available
    if (enclave_id != 0) {
        crypto_status_t status = CryptoUtils::decrypt_entry(decrypted, enclave_id);
        if (status != CRYPTO_SUCCESS) {
            std::cerr << "Warning: Decryption failed with status " << status 
                      << ", using entry as-is" << std::endl;
        }
    } else {
        // Fallback: just mark as decrypted for testing without SGX
        decrypted.is_encrypted = false;
    }
    
    return decrypted;
}