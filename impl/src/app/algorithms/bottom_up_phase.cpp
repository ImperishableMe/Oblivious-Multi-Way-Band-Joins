#include "bottom_up_phase.h"
#include "../data_structures/join_attribute_setter.h"
#include <iostream>
#include "../../common/debug_util.h"
#include "../Enclave_u.h"
#include "../../enclave/enclave_types.h"  // For METADATA_* constants

// Forward declarations for selective debug dumping
void debug_dump_selected_columns(const Table& table, const char* label, const char* step_name, 
                                 uint32_t eid, const std::vector<std::string>& columns);
void debug_dump_with_mask(const Table& table, const char* label, const char* step_name,
                          uint32_t eid, uint32_t column_mask);

void BottomUpPhase::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    std::cout << "Starting Bottom-Up Phase..." << std::endl;
    
    // Step 1: Initialize all tables with metadata
    InitializeAllTables(root, eid);
    
    // Step 2: Post-order traversal
    auto nodes = PostOrderTraversal(root);
    
    // Step 3: Process each node (all tables already have local_mult = 1)
    for (auto& node : nodes) {
        if (node->is_leaf()) {
            // Leaf nodes keep their initial local_mult = 1
            std::cout << "  Leaf node ready: " << node->get_table_name() << std::endl;
        } else {
            // Process internal nodes - multiply their local_mult by child contributions
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
    
    // Debug: Dump final local_mult values after bottom-up phase
    std::vector<JoinTreeNodePtr> all_nodes;
    std::function<void(JoinTreeNodePtr)> collect = [&](JoinTreeNodePtr node) {
        all_nodes.push_back(node);
        for (auto& child : node->get_children()) {
            collect(child);
        }
    };
    collect(root);
    
    for (const auto& node : all_nodes) {
        // Track more columns: original_index, local_mult, final_mult, field_type, equality_type
        uint32_t mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                       DEBUG_COL_FINAL_MULT | DEBUG_COL_FOREIGN_SUM |
                       DEBUG_COL_FIELD_TYPE | DEBUG_COL_EQUALITY_TYPE | 
                       DEBUG_COL_JOIN_ATTR;
        std::string step_name = "bottomup_step12_final_" + node->get_table_name();
        debug_dump_with_mask(node->get_table(), node->get_table_name().c_str(), step_name.c_str(), eid, mask);
    }
    
    std::cout << "Bottom-Up Phase completed." << std::endl;
}

void BottomUpPhase::InitializeAllTables(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    // Initialize ALL metadata fields to NULL_VALUE for clarity in debugging
    // Then set specific values as needed
    
    // First batch: Initialize all metadata to NULL_VALUE
    int32_t params[1] = { METADATA_ALL };
    Table temp = node->get_table().batched_map(eid, OP_ECALL_INIT_METADATA_NULL, params);
    
    // Second batch: Set local_mult to 1
    node->set_table(temp.batched_map(eid, OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE));
    
    // Set original indices using LinearPass with window function
    Table& table = node->get_table();
    if (table.size() > 0) {
        // Set first entry's index to 0
        entry_t first = table[0].to_entry_t();
        ecall_transform_set_index(eid, &first, 0);
        table[0].from_entry_t(first);
        
        // Use batched window function to set consecutive indices
        if (table.size() > 1) {
            table.batched_linear_pass(eid, OP_ECALL_WINDOW_SET_ORIGINAL_INDEX);
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
    DEBUG_INFO("INT_MAX=%d, INT_MAX/2=%d, INT_MIN=%d, INT_MIN/2=%d", 
               INT_MAX, INT_MAX/2, INT_MIN, INT_MIN/2);
    DEBUG_INFO("Is dev2 meant to be infinity? dev2=%d vs INT_MAX/2=%d", 
               dev2, INT_MAX/2);
    
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
    
    // Step 0: Set join_attr for both parent and child based on their join columns
    DEBUG_INFO("Setting join_attr for parent using column: %s", constraint.get_target_column().c_str());
    JoinAttributeSetter::SetJoinAttributesForTable(parent, constraint.get_target_column(), eid);
    
    DEBUG_INFO("Setting join_attr for child using column: %s", constraint.get_source_column().c_str());
    JoinAttributeSetter::SetJoinAttributesForTable(child, constraint.get_source_column(), eid);
    
    // Debug: Dump parent and child tables before combining  
    uint32_t debug_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                         DEBUG_COL_JOIN_ATTR | DEBUG_COL_ALL_ATTRIBUTES;
    debug_dump_with_mask(parent, "parent", "bottomup_step1_inputs", eid, debug_mask);
    debug_dump_with_mask(child, "child", "bottomup_step1_inputs", eid, debug_mask);
    
    // Step 1: Create combined table with dual-entry technique
    DEBUG_INFO("Creating combined table from parent (%zu) and child (%zu)", 
               parent.size(), child.size());
    Table combined = CombineTable(parent, child, constraint, eid);
    DEBUG_INFO("Combined table has %zu entries", combined.size());
    
    // Debug: Dump combined table after creation
    uint32_t combined_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                            DEBUG_COL_FIELD_TYPE | DEBUG_COL_EQUALITY_TYPE | 
                            DEBUG_COL_JOIN_ATTR | DEBUG_COL_ALL_ATTRIBUTES;
    debug_dump_with_mask(combined, "combined", "bottomup_step2_combine", eid, combined_mask);
    
    // Step 2: Initialize temporary fields (local_cumsum = local_mult, local_interval = 0)
    DEBUG_INFO("Initializing temporary fields");
    combined = combined.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_local_temps(eid, e);
        });
    DEBUG_INFO("Temporary fields initialized");
    
    // Debug: Dump after initializing temps
    uint32_t init_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE | 
                        DEBUG_COL_EQUALITY_TYPE | DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | 
                        DEBUG_COL_LOCAL_CUMSUM | DEBUG_COL_LOCAL_INTERVAL;
    debug_dump_with_mask(combined, "initialized", "bottomup_step3_init_temps", eid, init_mask);
    
    // Step 3: Sort by join attribute and precedence
    DEBUG_INFO("Sorting combined table by join attribute - BATCHED");
    combined.batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    DEBUG_INFO("Sort completed");
    
    // Debug: Dump after sorting by join attribute
    debug_dump_with_mask(combined, "sorted_by_join", "bottomup_step4_sorted", eid, init_mask);
    
    // Step 4: Compute local cumulative sums
    DEBUG_INFO("Computing local cumulative sums");
    combined.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_compute_local_sum(eid, e1, e2);
        });
    
    // Debug: Dump after computing cumulative sums
    debug_dump_with_mask(combined, "with_cumsum", "bottomup_step5_cumsum", eid, init_mask);
    
    // Step 5: Sort for pairwise processing (group START/END pairs)
    DEBUG_INFO("Sorting for pairwise processing");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_pairwise(eid, e1, e2);
        });
    
    // Debug: Dump after sorting for pairwise
    debug_dump_with_mask(combined, "sorted_pairwise", "bottomup_step6_pairwise", eid, init_mask);
    
    // Step 6: Compute intervals between START/END pairs
    DEBUG_INFO("Computing intervals between START/END pairs");
    combined.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_compute_local_interval(eid, e1, e2);
        });
    
    // Debug: Dump after computing intervals
    debug_dump_with_mask(combined, "with_intervals", "bottomup_step7_intervals", eid, init_mask);
    
    // Step 7: Sort END entries first for final update
    DEBUG_INFO("Sorting END entries first");
    combined.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_end_first(eid, e1, e2);
        });
    
    // Debug: Dump after sorting END first
    debug_dump_with_mask(combined, "sorted_end_first", "bottomup_step8_end_first", eid, init_mask);
    
    // Step 8: Truncate to parent size - now we have END entries with computed intervals
    // The first parent.size() entries are now the END entries with computed intervals
    DEBUG_INFO("Truncating to %zu entries (parent size)", parent.size());
    Table truncated;
    truncated.set_table_name("truncated");
    
    for (size_t i = 0; i < parent.size() && i < combined.size(); i++) {
        truncated.add_entry(combined[i]);
    }
    
    // Debug: Dump truncated END entries with intervals
    uint32_t key_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                       DEBUG_COL_LOCAL_INTERVAL | DEBUG_COL_FIELD_TYPE;
    debug_dump_with_mask(truncated, "truncated_ends", "bottomup_step9_truncated", eid, key_mask);
    
    // Step 9: Update parent multiplicities using parallel pass
    // This multiplies parent's local_mult by the computed interval from END entries
    DEBUG_INFO("Updating parent multiplicities");
    
    // Debug: Parent before update
    debug_dump_with_mask(parent, "parent_before", "bottomup_step10_parent_before", eid, key_mask);
    
    truncated.parallel_pass(parent, eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            // e1 is from truncated (source with intervals), e2 is from parent (target to update)
            return ecall_update_target_multiplicity(eid, e2, e1);
        });
    
    // Debug: Parent after update
    debug_dump_with_mask(parent, "parent_after", "bottomup_step11_parent_after", eid, key_mask);
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