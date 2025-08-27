#include "top_down_phase.h"
#include <iostream>
#include "../../common/debug_util.h"
#include "../Enclave_u.h"

void TopDownPhase::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    std::cout << "Starting Top-Down Phase..." << std::endl;
    
    // Step 1: Initialize ONLY root table with final_mult = local_mult
    InitializeRootTable(root, eid);
    
    // Step 2: Pre-order traversal (root to leaves)
    auto nodes = PreOrderTraversal(root);
    
    // Step 3: Process each node (skip root as it's already initialized)
    for (size_t i = 1; i < nodes.size(); i++) {
        auto& node = nodes[i];
        auto parent = node->get_parent();
        
        if (parent) {
            std::cout << "  Processing node: " << node->get_table_name() 
                      << " (parent: " << parent->get_table_name() << ")" << std::endl;
            
            // Compute foreign multiplicities from parent to child
            ComputeForeignMultiplicities(
                parent->get_table(),
                node->get_table(),
                node->get_constraint_with_parent(),
                eid);
        }
    }
    
    std::cout << "Top-Down Phase completed." << std::endl;
}

void TopDownPhase::InitializeRootTable(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    // Initialize root table only: final_mult = local_mult
    std::cout << "  Initializing root table: " << node->get_table_name() << std::endl;
    node->set_table(node->get_table().map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_final_mult(eid, e);
        }));
}

void TopDownPhase::InitializeForeignFields(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    // Initialize foreign-related fields to 0
    node->set_table(node->get_table().map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_foreign_temps(eid, e);
        }));
}

Table TopDownPhase::CombineTableForForeign(
    const Table& parent,
    const Table& child,
    const JoinConstraint& constraint,
    sgx_enclave_id_t eid) {
    
    DEBUG_INFO("CombineTableForForeign: parent=%zu entries, child=%zu entries", 
               parent.size(), child.size());
    
    // Get constraint parameters
    auto params = constraint.get_params();
    int32_t dev1 = params.deviation1;
    int32_t dev2 = params.deviation2;
    equality_type_t eq1 = params.equality1;
    equality_type_t eq2 = params.equality2;
    
    // Transform parent entries to SOURCE type (parent provides multiplicities)
    DEBUG_INFO("Transforming parent entries to SOURCE type");
    Table source_entries = parent.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_to_source(eid, e);
        });
    
    // Transform child entries to START boundaries (child receives multiplicities)
    DEBUG_INFO("Transforming child entries to START boundaries");
    Table start_entries = child.map(eid,
        [dev1, eq1](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_to_start(eid, e, dev1, eq1);
        });
    
    // Transform child entries to END boundaries
    DEBUG_INFO("Transforming child entries to END boundaries");
    Table end_entries = child.map(eid,
        [dev2, eq2](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_to_end(eid, e, dev2, eq2);
        });
    
    // Combine all three tables
    Table combined;
    combined.set_table_name("combined_foreign");
    
    // Add source entries (parent)
    for (const auto& entry : source_entries) {
        combined.add_entry(entry);
    }
    
    // Add start entries (child)
    for (const auto& entry : start_entries) {
        combined.add_entry(entry);
    }
    
    // Add end entries (child)
    for (const auto& entry : end_entries) {
        combined.add_entry(entry);
    }
    
    DEBUG_INFO("Combined table created with %zu entries", combined.size());
    
    return combined;
}

void TopDownPhase::ComputeForeignMultiplicities(
    Table& parent,
    Table& child,
    const JoinConstraint& constraint,
    sgx_enclave_id_t eid) {
    
    // Step 1: Create combined table for foreign computation
    DEBUG_INFO("Creating combined table for foreign computation");
    Table combined = CombineTableForForeign(parent, child, constraint, eid);
    debug_dump_table(combined, "combined_foreign", "topdown_step1_combined", eid);
    
    // Step 2: Initialize foreign temporary fields
    DEBUG_INFO("Initializing foreign temporary fields");
    combined = combined.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_foreign_temps(eid, e);
        });
    debug_dump_table(combined, "combined_foreign", "topdown_step2_init", eid);
    
    // Step 3: Sort by join attribute
    DEBUG_INFO("Sorting by join attribute");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_join_attr(eid, e1, e2);
        });
    debug_dump_table(combined, "combined_foreign", "topdown_step3_sorted", eid);
    
    // Step 4: Compute foreign cumulative sums and weights
    DEBUG_INFO("Computing foreign cumulative sums");
    combined.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_compute_foreign_sum(eid, e1, e2);
        });
    debug_dump_table(combined, "combined_foreign", "topdown_step4_foreign_sum", eid);
    
    // Step 5: Sort for pairwise processing
    DEBUG_INFO("Sorting for pairwise processing");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_pairwise(eid, e1, e2);
        });
    debug_dump_table(combined, "combined_foreign", "topdown_step5_pairwise", eid);
    
    // Step 6: Compute foreign intervals
    DEBUG_INFO("Computing foreign intervals");
    combined.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_compute_foreign_interval(eid, e1, e2);
        });
    debug_dump_table(combined, "combined_foreign", "topdown_step6_intervals", eid);
    
    // Step 7: Sort END entries first to extract computed intervals
    DEBUG_INFO("Sorting END entries first");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_end_first(eid, e1, e2);
        });
    debug_dump_table(combined, "combined_foreign", "topdown_step7_end_first", eid);
    
    // Step 8: Truncate to child size
    DEBUG_INFO("Truncating to %zu entries (child size)", child.size());
    Table truncated;
    truncated.set_table_name("truncated_foreign");
    
    for (size_t i = 0; i < child.size() && i < combined.size(); i++) {
        truncated.add_entry(combined[i]);
    }
    debug_dump_table(truncated, "truncated_foreign", "topdown_step8_truncated", eid);
    
    // Step 9: Update child's final multiplicities
    DEBUG_INFO("Updating child's final multiplicities");
    debug_dump_table(child, "child_before", "topdown_step9a_child_before", eid);
    truncated.parallel_pass(child, eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            // e1 is from truncated (source with foreign intervals)
            // e2 is from child (target to update)
            return ecall_update_target_final_multiplicity(eid, e2, e1);
        });
    DEBUG_INFO("Child final multiplicities updated");
    debug_dump_table(child, "child_after", "topdown_step9b_child_after", eid);
}

std::vector<JoinTreeNodePtr> TopDownPhase::PreOrderTraversal(JoinTreeNodePtr root) {
    std::vector<JoinTreeNodePtr> result;
    
    // Visit current node first (pre-order)
    result.push_back(root);
    
    // Then visit children recursively
    for (auto& child : root->get_children()) {
        auto child_nodes = PreOrderTraversal(child);
        result.insert(result.end(), child_nodes.begin(), child_nodes.end());
    }
    
    return result;
}