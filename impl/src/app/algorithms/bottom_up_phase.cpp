#include "bottom_up_phase.h"
#include <iostream>
#include "../../common/debug_util.h"
#include "../app/Enclave_u.h"

void BottomUpPhase::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    std::cout << "Starting Bottom-Up Phase..." << std::endl;
    
    // Step 1: Initialize all tables with metadata
    InitializeAllTables(root, eid);
    
    // Step 2: Post-order traversal
    auto nodes = PostOrderTraversal(root);
    
    // Step 3: Process each node
    for (auto& node : nodes) {
        if (node->is_leaf()) {
            // Initialize leaf multiplicities
            std::cout << "  Initializing leaf node: " << node->get_table_name() << std::endl;
            
            node->set_table(node->get_table().map(eid,
                [](sgx_enclave_id_t eid, entry_t* e) {
                    return ecall_transform_initialize_leaf(eid, e);
                }));
        } else {
            // Process each child
            std::cout << "  Processing internal node: " << node->get_table_name() << std::endl;
            
            for (auto& child : node->get_children()) {
                std::cout << "    Joining with child: " << child->get_table_name() << std::endl;
                
                ComputeLocalMultiplicities(
                    node->get_table(),  // parent (target)
                    child->get_table(), // child (source)
                    child->get_constraint_with_parent(),
                    eid);
            }
        }
    }
    
    std::cout << "Bottom-Up Phase completed." << std::endl;
}

void BottomUpPhase::InitializeAllTables(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    // Add metadata columns to current node's table
    node->set_table(node->get_table().map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_add_metadata(eid, e);
        }));
    
    // Set original indices using LinearPass with window function
    Table& table = node->get_table();
    if (table.size() > 0) {
        // Set first entry's index to 0
        entry_t first = table[0].to_entry_t();
        ecall_transform_set_index(eid, &first, 0);
        table[0].from_entry_t(first);
        
        // Use window function to set consecutive indices
        if (table.size() > 1) {
            table.linear_pass(eid,
                [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
                    return ecall_window_set_original_index(eid, e1, e2);
                });
        }
    }
    
    // Recursively initialize children
    for (auto& child : node->get_children()) {
        InitializeAllTables(child, eid);
    }
}

Table BottomUpPhase::CombineTable(
    const Table& target,
    const Table& source,
    const JoinConstraint& constraint,
    sgx_enclave_id_t eid) {
    
    DEBUG_INFO("CombineTable: target=%zu entries, source=%zu entries", 
               target.size(), source.size());
    
    // Get constraint parameters
    auto params = constraint.get_params();
    int32_t dev1 = params.deviation1;
    int32_t dev2 = params.deviation2;
    equality_type_t eq1 = params.equality1;
    equality_type_t eq2 = params.equality2;
    
    DEBUG_INFO("Constraint params: dev1=%d, dev2=%d, eq1=%d, eq2=%d", 
               dev1, dev2, eq1, eq2);
    
    // Transform source entries to SOURCE type
    DEBUG_INFO("Transforming source entries to SOURCE type");
    Table source_entries = source.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_to_source(eid, e);
        });
    
    // Transform target entries to START boundaries
    DEBUG_INFO("Transforming target entries to START boundaries");
    Table start_entries = target.map(eid,
        [dev1, eq1](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_to_start(eid, e, dev1, eq1);
        });
    
    // Transform target entries to END boundaries
    DEBUG_INFO("Transforming target entries to END boundaries");
    Table end_entries = target.map(eid,
        [dev2, eq2](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_to_end(eid, e, dev2, eq2);
        });
    
    // Combine all three tables
    DEBUG_INFO("Combining tables: source=%zu, start=%zu, end=%zu",
               source_entries.size(), start_entries.size(), end_entries.size());
    Table combined;
    combined.set_table_name("combined");
    
    // Add source entries
    DEBUG_INFO("Adding source entries");
    for (const auto& entry : source_entries) {
        combined.add_entry(entry);
    }
    
    // Add start entries
    DEBUG_INFO("Adding start entries");
    for (const auto& entry : start_entries) {
        combined.add_entry(entry);
    }
    
    // Add end entries
    DEBUG_INFO("Adding end entries");
    for (const auto& entry : end_entries) {
        combined.add_entry(entry);
    }
    
    DEBUG_INFO("Combined table created with %zu entries", combined.size());
    
    return combined;
}

void BottomUpPhase::ComputeLocalMultiplicities(
    Table& parent,
    Table& child,
    const JoinConstraint& constraint,
    sgx_enclave_id_t eid) {
    
    // Step 1: Create combined table with dual-entry technique
    DEBUG_INFO("Creating combined table from parent (%zu) and child (%zu)", 
               parent.size(), child.size());
    Table combined = CombineTable(parent, child, constraint, eid);
    DEBUG_INFO("Combined table has %zu entries", combined.size());
    
    // Step 2: Initialize temporary fields (local_cumsum = local_mult, local_interval = 0)
    DEBUG_INFO("Initializing temporary fields");
    combined = combined.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_local_temps(eid, e);
        });
    DEBUG_INFO("Temporary fields initialized");
    
    // Step 3: Sort by join attribute and precedence
    DEBUG_INFO("Sorting combined table by join attribute");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_join_attr(eid, e1, e2);
        });
    DEBUG_INFO("Sort completed");
    
    // Step 4: Compute local cumulative sums
    DEBUG_INFO("Computing local cumulative sums");
    combined.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_compute_local_sum(eid, e1, e2);
        });
    
    // Step 5: Sort for pairwise processing (group START/END pairs)
    DEBUG_INFO("Sorting for pairwise processing");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_pairwise(eid, e1, e2);
        });
    
    // Step 6: Compute intervals between START/END pairs
    DEBUG_INFO("Computing intervals between START/END pairs");
    combined.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_compute_local_interval(eid, e1, e2);
        });
    
    // Step 7: Sort END entries first for final update
    DEBUG_INFO("Sorting END entries first");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_end_first(eid, e1, e2);
        });
    
    // Step 8: Truncate to parent size - now we have END entries with computed intervals
    // The first parent.size() entries are now the END entries with computed intervals
    DEBUG_INFO("Truncating to %zu entries (parent size)", parent.size());
    Table truncated;
    truncated.set_table_name("truncated");
    
    for (size_t i = 0; i < parent.size() && i < combined.size(); i++) {
        truncated.add_entry(combined[i]);
    }
    
    // Step 9: Update parent multiplicities using parallel pass
    // This multiplies parent's local_mult by the computed interval from END entries
    DEBUG_INFO("Updating parent multiplicities");
    truncated.parallel_pass(parent, eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_update_target_multiplicity(eid, e1, e2);
        });
    DEBUG_INFO("Parent multiplicities updated");
}

std::vector<JoinTreeNodePtr> BottomUpPhase::PostOrderTraversal(JoinTreeNodePtr root) {
    std::vector<JoinTreeNodePtr> result;
    
    // Visit children first (recursively)
    for (auto& child : root->get_children()) {
        auto child_nodes = PostOrderTraversal(child);
        result.insert(result.end(), child_nodes.begin(), child_nodes.end());
    }
    
    // Then visit current node
    result.push_back(root);
    
    return result;
}