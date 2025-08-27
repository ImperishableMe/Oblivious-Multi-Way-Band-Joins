#include "distribute_expand.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include "../../common/debug_util.h"
#include "../Enclave_u.h"

// Forward declaration for table debugging
void debug_dump_table(const Table& table, const char* label, const char* step_name, uint32_t eid);

void DistributeExpand::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    std::cout << "Starting Distribute-Expand Phase..." << std::endl;
    
    // Get all nodes to expand
    auto nodes = GetAllNodes(root);
    
    // Expand each table according to its final multiplicities
    for (auto& node : nodes) {
        std::cout << "  Expanding table: " << node->get_table_name() << std::endl;
        
        Table expanded = ExpandSingleTable(node->get_table(), eid);
        node->set_table(expanded);
        
        std::cout << "    Original size: " << node->get_table().size() 
                  << " -> Expanded size: " << expanded.size() << std::endl;
    }
    
    std::cout << "Distribute-Expand Phase completed." << std::endl;
}

Table DistributeExpand::ExpandSingleTable(const Table& table, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        DEBUG_INFO("Empty table, nothing to expand");
        return table;  // Empty table, nothing to expand
    }
    
    DEBUG_INFO("Expanding table with %zu entries", table.size());
    
    // Step 1: Initialize dst_idx field to 0
    DEBUG_INFO("Step 1 - Initializing dst_idx");
    debug_dump_table(table, "input", "dist_step0_input", eid);
    Table working = table.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_dst_idx(eid, e);
        });
    DEBUG_INFO("Step 1 complete");
    debug_dump_table(working, "working", "dist_step1_init_dst", eid);
    
    // Step 2: Compute cumulative sum of final_mult to get dst_idx
    DEBUG_INFO("Step 2 - Computing cumulative sum");
    working.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_compute_dst_idx(eid, e1, e2);
        });
    DEBUG_INFO("Step 2 complete");
    debug_dump_table(working, "working", "dist_step2_cumsum", eid);
    
    // Step 3: Get output size from last entry
    DEBUG_INFO("Step 3 - Getting output size");
    size_t output_size = ComputeOutputSize(working, eid);
    DEBUG_INFO("Output size will be %zu", output_size);
    
    if (output_size == 0) {
        // All entries have final_mult = 0
        Table empty;
        empty.set_table_name(table.get_table_name());
        return empty;
    }
    
    // Step 4: Mark entries with final_mult = 0 as DIST_PADDING
    DEBUG_INFO("Step 4 - Marking entries with final_mult=0 as padding");
    working = working.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_mark_zero_mult_padding(eid, e);
        });
    DEBUG_INFO("Step 4 complete, table size=%zu", working.size());
    debug_dump_table(working, "working", "dist_step4_marked", eid);
    
    // Step 5: Sort to move DIST_PADDING entries to the end
    DEBUG_INFO("Step 5 - Sorting (size=%zu)", working.size());
    working.oblivious_sort(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_padding_last(eid, e1, e2);
        });
    DEBUG_INFO("Step 5 complete, table size after sort=%zu", working.size());
    debug_dump_table(working, "working", "dist_step5_sorted", eid);
    
    // Step 5b: Truncate table to remove excess DIST_PADDING entries
    // This handles cases where output_size < original_size
    if (working.size() > output_size) {
        DEBUG_INFO("Step 5b - Truncating table from %zu to %zu entries", working.size(), output_size);
        Table truncated;
        truncated.set_table_name(working.get_table_name());
        for (size_t i = 0; i < output_size; i++) {
            truncated.add_entry(working[i]);
        }
        working = truncated;
        DEBUG_INFO("Step 5b complete, table size after truncation=%zu", working.size());
        debug_dump_table(working, "working", "dist_step5b_truncated", eid);
    }
    
    // Step 6: Add padding entries to reach output_size
    size_t current_size = working.size();
    DEBUG_INFO("Step 6 - Adding padding entries: current_size=%zu, output_size=%zu", current_size, output_size);
    for (size_t i = current_size; i < output_size; i++) {
        entry_t padding;
        memset(&padding, 0, sizeof(entry_t));
        // Initialize padding entry
        ecall_transform_create_dist_padding(eid, &padding);
        working.add_entry(Entry(padding));
    }
    DEBUG_INFO("Step 6 complete, table size after padding=%zu", working.size());
    debug_dump_table(working, "working", "dist_step6_padded", eid);
    
    // Step 7: Initialize index field (0 to output_size-1)
    DEBUG_INFO("Step 7 - Initializing index field");
    working = working.map(eid,
        [](sgx_enclave_id_t eid, entry_t* e) {
            return ecall_transform_init_index(eid, e);
        });
    
    working.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_increment_index(eid, e1, e2);
        });
    DEBUG_INFO("Step 7 complete, table size=%zu", working.size());
    debug_dump_table(working, "working", "dist_step7_indexed", eid);
    
    // Step 8: Distribution phase using variable-distance passes
    DEBUG_INFO("Step 8 - Distribution phase");
    DistributePhase(working, output_size, eid);
    DEBUG_INFO("Step 8 complete, table size=%zu", working.size());
    debug_dump_table(working, "working", "dist_step8_distributed", eid);
    
    // Step 9: Expansion phase to fill gaps
    DEBUG_INFO("Step 9 - Expansion phase");
    ExpansionPhase(working, eid);
    DEBUG_INFO("Step 9 complete, final table size=%zu", working.size());
    debug_dump_table(working, "working", "dist_step9_final", eid);
    
    return working;
}

size_t DistributeExpand::ComputeOutputSize(const Table& table, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        return 0;
    }
    
    // Get the last entry's dst_idx + final_mult
    entry_t last_entry = table[table.size() - 1].to_entry_t();
    int32_t output_size = 0;
    
    sgx_status_t status = ecall_obtain_output_size(eid, &output_size, &last_entry);
    if (status != SGX_SUCCESS) {
        DEBUG_ERROR("Failed to obtain output size: %d", status);
        return 0;
    }
    
    return static_cast<size_t>(output_size);
}

void DistributeExpand::DistributePhase(Table& table, size_t output_size, sgx_enclave_id_t eid) {
    if (output_size <= 1) {
        return;  // No distribution needed for single element
    }
    
    DEBUG_INFO("Starting distribution phase for %zu entries", output_size);
    
    // Calculate starting distance: 2^(ceil(log2(output_size)) - 1)
    size_t distance = 1;
    while ((distance << 1) <= output_size) {
        distance <<= 1;
    }
    distance >>= 1;  // This gives us 2^(ceil(log2(n))-1)
    
    DEBUG_INFO("Starting distance: %zu", distance);
    
    // Perform variable-distance passes directly on table
    // The table provides a special method for distribution passes
    while (distance >= 1) {
        DEBUG_DEBUG("Distribution pass with distance %zu", distance);
        
        // Use a special function that operates on pairs at given distance
        table.distribute_pass(eid, distance,
            [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2, size_t dist) {
                ecall_comparator_distribute(eid, e1, e2, static_cast<int32_t>(dist));
            });
        
        distance >>= 1;  // Halve the distance
    }
    
    DEBUG_INFO("Distribution phase completed");
}

void DistributeExpand::ExpansionPhase(Table& table, sgx_enclave_id_t eid) {
    DEBUG_INFO("Starting expansion phase");
    
    // Linear pass to copy non-empty entries to fill DIST_PADDING slots
    table.linear_pass(eid,
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_window_expand_copy(eid, e1, e2);
        });
    
    DEBUG_INFO("Expansion phase completed");
}

std::vector<JoinTreeNodePtr> DistributeExpand::GetAllNodes(JoinTreeNodePtr root) {
    std::vector<JoinTreeNodePtr> result;
    
    // Pre-order traversal
    result.push_back(root);
    
    for (auto& child : root->get_children()) {
        auto child_nodes = GetAllNodes(child);
        result.insert(result.end(), child_nodes.begin(), child_nodes.end());
    }
    
    return result;
}