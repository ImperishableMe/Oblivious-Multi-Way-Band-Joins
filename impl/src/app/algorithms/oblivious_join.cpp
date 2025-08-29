#include "oblivious_join.h"
#include "bottom_up_phase.h"
#include "top_down_phase.h"
#include "distribute_expand.h"
#include "align_concat.h"
#include <iostream>
#include <sstream>
#include <functional>
#include "../../common/debug_util.h"

// Forward declaration for table debugging

Table ObliviousJoin::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    // Validate the join tree
    if (!ValidateJoinTree(root)) {
        throw std::runtime_error("Invalid join tree structure");
    }
    
    // Check initial encryption state
    AssertTreeConsistentEncryption(root);
    
    // Phase 1: Bottom-Up - Compute local multiplicities
    AssertTreeConsistentEncryption(root);
    BottomUpPhase::Execute(root, eid);
    AssertTreeConsistentEncryption(root);
    
    // Phase 2: Top-Down - Compute final multiplicities
    AssertTreeConsistentEncryption(root);
    TopDownPhase::Execute(root, eid);
    AssertTreeConsistentEncryption(root);
    
    // Phase 3: Distribute-Expand - Replicate tuples
    AssertTreeConsistentEncryption(root);
    DistributeExpand::Execute(root, eid);
    AssertTreeConsistentEncryption(root);
    
    // Phase 4: Align-Concat - Construct result
    AssertTreeConsistentEncryption(root);
    Table result = AlignConcat::Execute(root, eid);
    
    // Check final result encryption state
    AssertConsistentEncryption(result);
    
    printf("Result: %zu rows\n", result.size());
    
    return result;
}

Table ObliviousJoin::ExecuteWithDebug(JoinTreeNodePtr root, 
                                       sgx_enclave_id_t eid,
                                       const std::string& session_name) {
    // Initialize debug session
    debug_init_session(session_name.c_str());
    
    DEBUG_INFO("Starting oblivious join with debug session: %s", session_name.c_str());
    
    // Dump initial tables
    auto nodes = std::vector<JoinTreeNodePtr>();
    std::function<void(JoinTreeNodePtr)> collect = [&](JoinTreeNodePtr node) {
        nodes.push_back(node);
        for (const auto& child : node->get_children()) {
            collect(child);
        }
    };
    collect(root);
    
    for (const auto& node : nodes) {
        std::string label = "input_" + node->get_table_name();
    }
    
    // Execute the join
    Table result = Execute(root, eid);
    
    // Dump final result
    
    // Close debug session
    debug_close_session();
    
    return result;
}

bool ObliviousJoin::ValidateJoinTree(JoinTreeNodePtr root) {
    if (!root) {
        std::cerr << "Error: Null root node" << std::endl;
        return false;
    }
    
    if (root->get_table().size() == 0) {
        std::cerr << "Error: Root table is empty" << std::endl;
        return false;
    }
    
    // Recursively validate children
    for (const auto& child : root->get_children()) {
        if (!child) {
            std::cerr << "Error: Null child node" << std::endl;
            return false;
        }
        
        if (child->get_table().size() == 0) {
            std::cerr << "Error: Child table " << child->get_table_name() 
                      << " is empty" << std::endl;
            return false;
        }
        
        // Check that child has constraint with parent
        // Note: All children should have constraints set when added via add_child
        // We'll validate by checking if the constraint has valid parameters
        
        // Recursively validate subtree
        if (!ValidateJoinTree(child)) {
            return false;
        }
    }
    
    return true;
}

void ObliviousJoin::LogJoinTree(JoinTreeNodePtr root, int level) {
    if (!root) return;
    
    // Print indentation
    for (int i = 0; i < level; i++) {
        std::cout << "  ";
    }
    
    // Print node info
    std::cout << "- " << root->get_table_name() 
              << " (" << root->get_table().size() << " rows)";
    
    // Print constraint if this is a child
    if (level > 0) {
        try {
            auto constraint = root->get_constraint_with_parent();
            auto params = constraint.get_params();
            std::cout << " [join on attr with deviations "
                      << params.deviation1 << ", " << params.deviation2 << "]";
        } catch (...) {
            // No constraint with parent
        }
    }
    
    std::cout << std::endl;
    
    // Recursively print children
    for (const auto& child : root->get_children()) {
        LogJoinTree(child, level + 1);
    }
}

std::string ObliviousJoin::GetJoinStatistics(JoinTreeNodePtr root) {
    std::stringstream ss;
    ss << "Join Statistics:\n";
    
    // Count total tables and rows
    int total_tables = 0;
    size_t total_rows = 0;
    size_t min_rows = SIZE_MAX;
    size_t max_rows = 0;
    
    std::function<void(JoinTreeNodePtr)> count = [&](JoinTreeNodePtr node) {
        total_tables++;
        size_t size = node->get_table().size();
        total_rows += size;
        min_rows = std::min(min_rows, size);
        max_rows = std::max(max_rows, size);
        
        for (const auto& child : node->get_children()) {
            count(child);
        }
    };
    count(root);
    
    ss << "  Total tables: " << total_tables << "\n";
    ss << "  Total input rows: " << total_rows << "\n";
    ss << "  Min table size: " << min_rows << "\n";
    ss << "  Max table size: " << max_rows << "\n";
    
    // Calculate tree depth
    std::function<int(JoinTreeNodePtr)> depth = [&](JoinTreeNodePtr node) -> int {
        if (node->get_children().empty()) return 0;
        int max_child_depth = 0;
        for (const auto& child : node->get_children()) {
            max_child_depth = std::max(max_child_depth, depth(child));
        }
        return 1 + max_child_depth;
    };
    
    ss << "  Tree depth: " << depth(root) << "\n";
    
    // Count leaf nodes
    int leaf_count = 0;
    std::function<void(JoinTreeNodePtr)> count_leaves = [&](JoinTreeNodePtr node) {
        if (node->get_children().empty()) {
            leaf_count++;
        } else {
            for (const auto& child : node->get_children()) {
                count_leaves(child);
            }
        }
    };
    count_leaves(root);
    
    ss << "  Leaf tables: " << leaf_count;
    
    return ss.str();
}