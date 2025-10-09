#include "distribute_expand.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include "debug_util.h"
#include "../utils/counted_ecalls.h"  // Includes both Enclave_u.h and ecall_wrapper.h

// Debug functions are declared in debug_util.h
// debug_dump_table is already declared in debug_util.h

void DistributeExpand::Execute(JoinTreeNodePtr root) {
    
    // Get all nodes to expand
    auto nodes = GetAllNodes(root);
    
    // Debug: Check tables right after getting nodes
    DEBUG_INFO("Distribute-Expand: Checking tables after GetAllNodes");
    for (auto& node : nodes) {
        if (node->get_table().size() > 0) {
            Entry first = node->get_table()[0];
            DEBUG_INFO("  Table %s[0]: field_type=%d, equality_type=%d",
                       node->get_table_name().c_str(), 
                       first.field_type, first.equality_type);
        }
    }
    
    // Expand each table according to its final multiplicities
    for (auto& node : nodes) {
        // Expanding table
        
        // Debug: Check table before expansion
        DEBUG_INFO("Before ExpandSingleTable for %s", node->get_table_name().c_str());
        if (node->get_table().size() > 0) {
            Entry first = node->get_table()[0];
            DEBUG_INFO("  field_type=%d, equality_type=%d", 
                       first.field_type, first.equality_type);
            
            // Dump table before expansion
            std::string label = "distexp_pre_expand_" + node->get_table_name();
        }
        
        Table expanded = ExpandSingleTable(node->get_table());
        node->set_table(expanded);
        
        // Expanded from original size to new size
    }
}

Table DistributeExpand::ExpandSingleTable(const Table& table) {
    if (table.size() == 0) {
        DEBUG_INFO("Empty table, nothing to expand");
        return table;  // Empty table, nothing to expand
    }
    
    DEBUG_INFO("Expanding table with %zu entries", table.size());
    
    // Get table name for debug output
    std::string table_name = table.get_table_name();
    DEBUG_INFO("Table name: %s", table_name.c_str());
    
    // Targeted debug: Check final_mult values before expansion
    uint32_t key_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                       DEBUG_COL_FINAL_MULT | DEBUG_COL_FIELD_TYPE;
    debug_dump_with_mask(table, ("pre_expand_" + table_name).c_str(), 
                        ("distexp_pre_expand_" + table_name).c_str(), static_cast<uint32_t>(), key_mask);
    
    // Step 1: Initialize dst_idx field to 0
    DEBUG_INFO("Step 1 - Initializing dst_idx");
    Table working = table.batched_map( OP_ECALL_TRANSFORM_INIT_DST_IDX);
    DEBUG_INFO("Step 1 complete");
    
    // Step 2: Compute cumulative sum of final_mult to get dst_idx
    DEBUG_INFO("Step 2 - Computing cumulative sum");
    working.batched_linear_pass( OP_ECALL_WINDOW_COMPUTE_DST_IDX);
    DEBUG_INFO("Step 2 complete");
    
    // Debug: Show dst_idx values after cumulative sum
    uint32_t dst_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FINAL_MULT | DEBUG_COL_DST_IDX;
    debug_dump_with_mask(working, ("step2_dst_idx_" + table_name).c_str(),
                        ("distexp_step2_cumsum_" + table_name).c_str(), static_cast<uint32_t>(), dst_mask);
    
    // Step 3: Get output size from last entry
    DEBUG_INFO("Step 3 - Getting output size");
    size_t output_size = ComputeOutputSize(working);
    DEBUG_INFO("Output size will be %zu", output_size);
    
    if (output_size == 0) {
        // All entries have final_mult = 0
        Table empty(table.get_table_name(), table.get_schema());
        return empty;
    }
    
    // Step 4: Mark entries with final_mult = 0 as DIST_PADDING
    DEBUG_INFO("Step 4 - Marking entries with final_mult=0 as padding");
    working = working.batched_map( OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING);
    DEBUG_INFO("Step 4 complete, table size=%zu", working.size());
    
    // Debug: Show which entries are marked as padding
    uint32_t padding_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FINAL_MULT | 
                           DEBUG_COL_FIELD_TYPE | DEBUG_COL_DST_IDX;
    debug_dump_with_mask(working, ("step4_marked_padding_" + table_name).c_str(),
                        ("distexp_step4_padding_" + table_name).c_str(), static_cast<uint32_t>(), padding_mask);
    
    // Step 5: Sort to move DIST_PADDING entries to the end
    DEBUG_INFO("Step 5 - Sorting (size=%zu)", working.size());
    working.shuffle_merge_sort( OP_ECALL_COMPARATOR_PADDING_LAST);
    DEBUG_INFO("Step 5 complete, table size after sort=%zu", working.size());
    
    // Step 5b: Truncate table to remove excess DIST_PADDING entries
    // This handles cases where output_size < original_size
    if (working.size() > output_size) {
        DEBUG_INFO("Step 5b - Truncating table from %zu to %zu entries", working.size(), output_size);
        Table truncated(working.get_table_name(), working.get_schema());
        for (size_t i = 0; i < output_size; i++) {
            truncated.add_entry(working[i]);
        }
        working = truncated;
        DEBUG_INFO("Step 5b complete, table size after truncation=%zu", working.size());
    }
    
    // Step 6: Add padding entries to reach output_size
    size_t current_size = working.size();
    DEBUG_INFO("Step 6 - Adding padding entries: current_size=%zu, output_size=%zu", current_size, output_size);
    
    // Get the table's encryption status (asserts consistency)
    uint8_t table_encryption_status = AssertConsistentEncryption(working);
    
    // Use batched padding creation for efficiency
    size_t padding_needed = output_size - current_size;
    if (padding_needed > 0) {
        working.add_batched_padding(padding_needed, eid, table_encryption_status);
    }
    DEBUG_INFO("Step 6 complete, table size after padding=%zu", working.size());
    
    // Step 7: Initialize index field (0 to output_size-1)
    DEBUG_INFO("Step 7 - Initializing index field");
    working = working.batched_map( OP_ECALL_TRANSFORM_INIT_INDEX);
    
    working.batched_linear_pass( OP_ECALL_WINDOW_INCREMENT_INDEX);
    DEBUG_INFO("Step 7 complete, table size=%zu", working.size());
    
    // Step 7b: Debug dump before distribution - shows initial state with non-padding at top
    uint32_t before_dist_mask = DEBUG_COL_INDEX | DEBUG_COL_ORIGINAL_INDEX | 
                               DEBUG_COL_FINAL_MULT | DEBUG_COL_DST_IDX | 
                               DEBUG_COL_FIELD_TYPE;
    debug_dump_with_mask(working, ("step7_before_distribute_" + table_name).c_str(),
                        ("distexp_step7_before_dist_" + table_name).c_str(), static_cast<uint32_t>(), before_dist_mask);
    
    // Step 8: Distribution phase using variable-distance passes
    DEBUG_INFO("Step 8 - Distribution phase");
    DistributePhase(working, output_size);
    DEBUG_INFO("Step 8 complete, table size=%zu", working.size());
    
    // Step 9: Expansion phase to fill gaps
    DEBUG_INFO("Step 9 - Expansion phase");
    
    // Debug: Dump table before expansion copy
    debug_dump_table(working, ("before_expansion_copy_" + table_name).c_str(), 
                    ("distexp_step9a_before_" + table_name).c_str(), static_cast<uint32_t>());
    
    ExpansionPhase(working);
    
    // Debug: Dump table after expansion copy
    debug_dump_table(working, ("after_expansion_copy_" + table_name).c_str(), 
                    ("distexp_step9b_after_" + table_name).c_str(), static_cast<uint32_t>());
    
    DEBUG_INFO("Step 9 complete, final table size=%zu", working.size());
    
    // Step 10: Final debug dump showing complete expanded table
    DEBUG_INFO("Step 10 - Final expanded result");
    uint32_t final_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                         DEBUG_COL_FINAL_MULT | DEBUG_COL_COPY_INDEX | 
                         DEBUG_COL_DST_IDX | DEBUG_COL_FIELD_TYPE;
    debug_dump_with_mask(working, ("final_expanded_" + table_name).c_str(),
                        ("distexp_step10_final_" + table_name).c_str(), static_cast<uint32_t>(), final_mask);
    
    return working;
}

size_t DistributeExpand::ComputeOutputSize(const Table& table) {
    if (table.size() == 0) {
        return 0;
    }
    
    // Get the last entry's dst_idx + final_mult
    entry_t last_entry = table[table.size() - 1].to_entry_t();
    int32_t output_size = 0;
    
    sgx_status_t status = counted_ecall_obtain_output_size( &output_size, &last_entry);
    if (status != SGX_SUCCESS) {
        DEBUG_ERROR("Failed to obtain output size: %d", status);
        return 0;
    }
    
    return static_cast<size_t>(output_size);
}

void DistributeExpand::DistributePhase(Table& table, size_t output_size) {
    if (output_size <= 1) {
        return;  // No distribution needed for single element
    }
    
    DEBUG_INFO("Starting distribution phase for %zu entries", output_size);
    
    // Calculate starting distance: largest power of 2 <= output_size
    size_t distance = 1;
    while ((distance << 1) <= output_size) {
        distance <<= 1;
    }
    // distance now contains the largest power of 2 <= output_size
    
    DEBUG_INFO("Starting distance: %zu", distance);
    
    // Perform variable-distance passes directly on table
    // The table provides a special method for distribution passes
    while (distance >= 1) {
        DEBUG_DEBUG("Distribution pass with distance %zu", distance);
        
        // Use batched version for better performance
        table.batched_distribute_pass( distance, OP_ECALL_COMPARATOR_DISTRIBUTE);
        
        distance >>= 1;  // Halve the distance
    }
    
    DEBUG_INFO("Distribution phase completed");
}

void DistributeExpand::ExpansionPhase(Table& table) {
    DEBUG_INFO("Starting expansion phase");
    
    // Linear pass to copy non-empty entries to fill DIST_PADDING slots
    table.batched_linear_pass( OP_ECALL_WINDOW_EXPAND_COPY);
    
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