#include "align_concat.h"
#include <iostream>
#include "../../common/debug_util.h"
#include "../Enclave_u.h"

// Forward declaration for table debugging
void debug_dump_table(const Table& table, const char* label, const char* step_name, uint32_t eid);

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
    debug_dump_table(accumulator, "accumulator_initial", "align_root", eid);
    
    // Process each child in pre-order
    for (const auto& child : root->get_children()) {
        DEBUG_INFO("Aligning and concatenating child: %s", 
                   child->get_table_name().c_str());
        
        // Recursively get the result for the child subtree
        Table child_result = ConstructJoinResult(child, eid);
        
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
    DEBUG_INFO("Aligning tables: accumulator size=%zu, child size=%zu",
               accumulator.size(), child.size());
    
    // Step 1: Sort accumulator by join attribute, then other attributes
    DEBUG_INFO("Step 1: Sorting accumulator by join attr, then others");
    Table sorted_accumulator = accumulator;
    sorted_accumulator.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_join_then_other(eid, e1, e2);
        });
    debug_dump_table(sorted_accumulator, "accumulator", "align_step1_sorted", eid);
    
    // Step 2: Compute copy indices for child table
    DEBUG_INFO("Step 2: Computing copy indices");
    Table indexed_child = ComputeCopyIndices(child, eid);
    debug_dump_table(indexed_child, "child", "align_step2_copy_indices", eid);
    
    // Step 3: Compute alignment keys
    DEBUG_INFO("Step 3: Computing alignment keys");
    Table aligned_child = ComputeAlignmentKeys(indexed_child, eid);
    debug_dump_table(aligned_child, "child", "align_step3_alignment_keys", eid);
    
    // Step 4: Sort child by alignment key
    DEBUG_INFO("Step 4: Sorting child by alignment key");
    aligned_child.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_alignment_key(eid, e1, e2);
        });
    debug_dump_table(aligned_child, "child", "align_step4_sorted", eid);
    
    // Step 5: Horizontal concatenation
    DEBUG_INFO("Step 5: Horizontal concatenation");
    Table result = Table::horizontal_concatenate(sorted_accumulator, aligned_child);
    debug_dump_table(result, "result", "align_step5_concatenated", eid);
    
    DEBUG_INFO("Alignment and concatenation complete. Result size: %zu", result.size());
    
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