#include "align_concat.h"
#include "../data_structures/join_attribute_setter.h"
#include <iostream>
#include "../../common/debug_util.h"
#include "../Enclave_u.h"

// Table debugging functions are declared in debug_util.h

Table AlignConcat::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    DEBUG_INFO("Starting Align-Concat Phase...");
    
    // Construct the join result by traversing the tree
    Table result = ConstructJoinResult(root, eid);
    
    DEBUG_INFO("Align-Concat Phase completed. Final result size: %zu", result.size());
    
    return result;
}

Table AlignConcat::ConstructJoinResult(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    DEBUG_INFO("Constructing join result starting from table: %s", 
               root->get_table_name().c_str());
    
    // Start with the root table as the accumulator
    Table accumulator = root->get_table();
    
    // Debug the initial accumulator
    
    // Process each child in pre-order
    for (const auto& child : root->get_children()) {
        DEBUG_INFO("Aligning and concatenating child: %s", 
                   child->get_table_name().c_str());
        
        // Get the child's join constraint with its parent
        const JoinConstraint& constraint = child->get_constraint_with_parent();
        
        // Set join_attr for accumulator to the child's join column with parent
        // This ensures proper alignment during concatenation
        DEBUG_INFO("Setting accumulator join_attr to: %s", constraint.get_target_column().c_str());
        JoinAttributeSetter::SetJoinAttributesForTable(accumulator, constraint.get_target_column(), eid);
        
        // Recursively get the result for the child subtree
        Table child_result = ConstructJoinResult(child, eid);
        
        // Set join_attr for child result to its join column with parent
        DEBUG_INFO("Setting child result join_attr to: %s", constraint.get_source_column().c_str());
        JoinAttributeSetter::SetJoinAttributesForTable(child_result, constraint.get_source_column(), eid);
        
        // Align and concatenate with accumulator
        accumulator = AlignAndConcatenate(accumulator, child_result, eid);
        
        DEBUG_INFO("Accumulator size after adding %s: %zu",
                   child->get_table_name().c_str(), accumulator.size());
    }
    
    return accumulator;
}

Table AlignConcat::AlignAndConcatenate(const Table& accumulator, 
                                       const Table& child,
                                       sgx_enclave_id_t eid) {
    static int concat_iteration = 0;
    concat_iteration++;
    
    DEBUG_INFO("========================================");
    DEBUG_INFO("=== CONCATENATION OPERATION #%d ===", concat_iteration);
    DEBUG_INFO("Aligning tables: accumulator size=%zu, child size=%zu",
               accumulator.size(), child.size());
    DEBUG_INFO("========================================");
    
    std::string concat_label = "concat" + std::to_string(concat_iteration);
    
    // Step 1: Sort accumulator by join attribute, then other attributes
    DEBUG_INFO("Step 1: Sorting accumulator by join attr, then others");
    Table sorted_accumulator = accumulator;
    sorted_accumulator.batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_JOIN_THEN_OTHER);
    
    // Step 2: Compute copy indices for child table
    DEBUG_INFO("Step 2: Computing copy indices");
    Table indexed_child = ComputeCopyIndices(child, eid);
    
    // Step 3: Compute alignment keys
    DEBUG_INFO("Step 3: Computing alignment keys");
    Table aligned_child = ComputeAlignmentKeys(indexed_child, eid);
    
    // Step 4: Sort child by alignment key
    DEBUG_INFO("Step 4: Sorting child by alignment key");
    aligned_child.batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_ALIGNMENT_KEY);
    
    // Step 5: Horizontal concatenation using parallel pass
    DEBUG_INFO("Step 5: Horizontal concatenation via parallel pass");
    
    // The result starts as a copy of the sorted accumulator
    Table result = sorted_accumulator;
    
    // CRITICAL DEBUG: Dump both tables RIGHT BEFORE concatenation
    // These are the two tables that will be concatenated horizontally
    DEBUG_INFO("=== TABLES IMMEDIATELY BEFORE HORIZONTAL CONCATENATION ===");
    
    // Dump sorted accumulator (left side of concatenation)
    debug_dump_table(result, ("before_concat_accumulator_" + concat_label).c_str(), 
                    ("align_step5a_before_concat_" + concat_label).c_str(), eid,
                    {META_INDEX, META_ORIG_IDX, META_LOCAL_MULT, META_FINAL_MULT, META_FOREIGN_SUM, META_COPY_INDEX, META_ALIGN_KEY, META_JOIN_ATTR}, true);
    
    // Dump aligned child (right side of concatenation)  
    debug_dump_table(aligned_child, ("before_concat_child_" + concat_label).c_str(),
                    ("align_step5b_before_concat_" + concat_label).c_str(), eid,
                    {META_INDEX, META_ORIG_IDX, META_LOCAL_MULT, META_FINAL_MULT, META_FOREIGN_SUM, META_COPY_INDEX, META_ALIGN_KEY, META_JOIN_ATTR}, true);
    
    DEBUG_INFO("Table sizes - Accumulator: %zu rows, Child: %zu rows", result.size(), aligned_child.size());
    DEBUG_INFO("These two tables will now be concatenated horizontally (parallel_pass)");
    
    // Debug: Print first few entries before concatenation
    if (result.size() > 0 && aligned_child.size() > 0) {
        DEBUG_INFO("Before concat - first accumulator entry:");
        Entry acc_first = result[0];
        DEBUG_INFO("  original_index=%d, join_attr=%d, final_mult=%d",
                   acc_first.original_index, acc_first.join_attr, acc_first.final_mult);
        
        DEBUG_INFO("Before concat - first child entry:");
        Entry child_first = aligned_child[0];
        DEBUG_INFO("  original_index=%d, join_attr=%d, alignment_key=%d, copy_index=%d",
                   child_first.original_index, child_first.join_attr, 
                   child_first.alignment_key, child_first.copy_index);
    }
    
    // Use parallel_pass to concatenate attributes from aligned_child
    result.parallel_pass(aligned_child, eid,
        [](sgx_enclave_id_t eid, entry_t* left, entry_t* right) {
            // This ecall will concatenate attributes from right into left
            return ecall_concat_attributes(eid, left, right);
        });
    
    // Debug: Dump final result - show concatenated attributes WITH ALL COLUMNS
    debug_dump_table(result, ("final_result_" + concat_label).c_str(), 
                    ("align_step5_" + concat_label).c_str(), eid,
                    {META_INDEX, META_ORIG_IDX, META_LOCAL_MULT, META_FINAL_MULT, META_FOREIGN_SUM, META_COPY_INDEX, META_ALIGN_KEY, META_JOIN_ATTR}, true);
    
    DEBUG_INFO("========================================");
    DEBUG_INFO("=== END CONCATENATION #%d ===", concat_iteration);
    DEBUG_INFO("Result size: %zu", result.size());
    DEBUG_INFO("========================================\n");
    
    return result;
}

Table AlignConcat::ComputeCopyIndices(const Table& table, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        return table;
    }
    
    DEBUG_DEBUG("Computing copy indices for %zu entries", table.size());
    
    // Initialize copy_index to 0 for all entries
    Table result = table.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_copy_index(eid, e);
        });
    
    // Linear pass to compute copy indices
    // Same original_index -> increment
    // Different original_index -> reset to 0
    result.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_update_copy_index(eid, e1, e2);
        });
    
    DEBUG_DEBUG("Copy indices computed");
    
    return result;
}

Table AlignConcat::ComputeAlignmentKeys(const Table& table, sgx_enclave_id_t eid) {
    DEBUG_DEBUG("Computing alignment keys for %zu entries", table.size());
    
    // Apply transformation to compute alignment_key = foreign_sum + (copy_index / local_mult)
    Table result = table.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_compute_alignment_key(eid, e);
        });
    
    DEBUG_DEBUG("Alignment keys computed");
    
    return result;
}

std::vector<JoinTreeNodePtr> AlignConcat::PreOrderTraversal(JoinTreeNodePtr root) {
    std::vector<JoinTreeNodePtr> result;
    
    // Visit current node first (pre-order)
    result.push_back(root);
    
    // Then visit children
    for (const auto& child : root->get_children()) {
        auto child_nodes = PreOrderTraversal(child);
        result.insert(result.end(), child_nodes.begin(), child_nodes.end());
    }
    
    return result;
}