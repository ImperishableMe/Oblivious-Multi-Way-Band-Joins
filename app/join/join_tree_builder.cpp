#include "join_tree_builder.h"
#include <queue>
#include <set>
#include <iostream>
#include <stdexcept>

namespace {

/**
 * The equalities carried by the edges of the join tree, as a union-find over
 * "table.column" names in which every column also records its constant
 * difference to the representative of its set. It answers: do the tree's
 * equalities already force a.x = b.y + k, and for which k?
 *
 * Used to tell a join condition that merely repeats what the tree already
 * guarantees (r.a = s.a AND s.a = u.a make r.a = u.a redundant) from one that
 * genuinely closes a cycle. Works on the query text only, never on data.
 */
class ColumnEqualities {
private:
    std::map<std::string, std::string> parent;
    std::map<std::string, int64_t> offset_to_parent;  // value(col) = value(parent) + offset

    // Representative of col's set; diff is set so that value(col) = value(rep) + diff
    std::string find(const std::string& col, int64_t& diff) {
        if (parent.find(col) == parent.end()) {
            parent[col] = col;
            offset_to_parent[col] = 0;
        }
        std::string current = col;
        diff = 0;
        while (parent[current] != current) {
            diff += offset_to_parent[current];
            current = parent[current];
        }
        return current;
    }

public:
    // Record a = b + k
    void add(const std::string& a, const std::string& b, int64_t k) {
        int64_t diff_a = 0;
        int64_t diff_b = 0;
        std::string rep_a = find(a, diff_a);
        std::string rep_b = find(b, diff_b);
        if (rep_a == rep_b) {
            return;  // already related; nothing new to record
        }
        // value(a) = value(rep_a) + diff_a, value(b) = value(rep_b) + diff_b and
        // a = b + k give value(rep_a) = value(rep_b) + diff_b + k - diff_a
        parent[rep_a] = rep_b;
        offset_to_parent[rep_a] = diff_b + k - diff_a;
    }

    // If the recorded equalities relate a and b, sets diff with a = b + diff
    bool related(const std::string& a, const std::string& b, int64_t& diff) {
        int64_t diff_a = 0;
        int64_t diff_b = 0;
        if (find(a, diff_a) != find(b, diff_b)) {
            return false;
        }
        diff = diff_a - diff_b;
        return true;
    }
};

std::string qualified(const std::string& table, const std::string& column) {
    return table + "." + column;
}

// Does source = target + k satisfy the constraint's main condition
// source IN [target + deviation1, target + deviation2]?
bool main_condition_holds_for(const JoinConstraint& c, int64_t k) {
    const int64_t lower = c.get_deviation1();
    const int64_t upper = c.get_deviation2();
    const bool lower_ok = (lower == JOIN_ATTR_NEG_INF) ||
                          (c.get_equality1() == EQ ? k >= lower : k > lower);
    const bool upper_ok = (upper == JOIN_ATTR_POS_INF) ||
                          (c.get_equality2() == EQ ? k <= upper : k < upper);
    return lower_ok && upper_ok;
}

enum class ImpliedStatus {
    IMPLIED,        // every part already follows from the tree's equalities
    CONTRADICTED,   // some part can never hold given the tree's equalities
    INDEPENDENT     // some part is a genuinely new condition
};

// Classify a condition that is not on a tree edge against the tree's equalities.
ImpliedStatus classify_against_tree(const JoinConstraint& c, ColumnEqualities& equalities) {
    const std::string& src = c.get_source_table();
    const std::string& tgt = c.get_target_table();
    bool every_part_implied = true;
    int64_t k = 0;

    if (equalities.related(qualified(src, c.get_source_column()),
                           qualified(tgt, c.get_target_column()), k)) {
        if (!main_condition_holds_for(c, k)) {
            return ImpliedStatus::CONTRADICTED;
        }
    } else {
        every_part_implied = false;
    }

    for (size_t i = 0; i < c.num_equalities(); i++) {
        if (equalities.related(qualified(src, c.get_source_eq_columns()[i]),
                               qualified(tgt, c.get_target_eq_columns()[i]), k)) {
            if (k != c.get_eq_offsets()[i]) {
                return ImpliedStatus::CONTRADICTED;
            }
        } else {
            every_part_implied = false;
        }
    }

    return every_part_implied ? ImpliedStatus::IMPLIED : ImpliedStatus::INDEPENDENT;
}

}  // namespace

JoinTreeNodePtr JoinTreeBuilder::build_from_query(
    const ParsedQuery& query,
    const std::map<std::string, Table>& tables) {
    
    // Default: use first table as root
    if (query.tables.empty()) {
        throw std::runtime_error("No tables in query");
    }
    
    return build_from_query_with_root(query, tables, query.tables[0]);
}

JoinTreeNodePtr JoinTreeBuilder::build_from_query_with_root(
    const ParsedQuery& query,
    const std::map<std::string, Table>& tables,
    const std::string& root_table) {
    
    // Validate inputs
    if (tables.find(root_table) == tables.end()) {
        throw std::runtime_error("Root table not found in table map: " + root_table);
    }
    
    // Create root node
    auto root = std::make_shared<JoinTreeNode>(root_table, tables.at(root_table));
    
    // Track visited tables
    std::set<std::string> visited;
    visited.insert(root_table);
    
    // Build tree recursively
    build_tree_recursive(root, visited, query.tables, query.join_conditions, tables);

    // The algorithm only evaluates conditions between a node and its parent,
    // so every join condition must either sit on an edge of the tree or add
    // nothing to it. Split the conditions into those two groups and collect
    // the equalities the tree edges carry.
    std::set<std::pair<std::string, std::string>> tree_edges;
    collect_tree_edges(root, tree_edges);

    ColumnEqualities tree_equalities;
    std::vector<JoinConstraint> off_tree_conditions;
    for (const auto& constraint : query.join_conditions) {
        const std::string& a = constraint.get_source_table();
        const std::string& b = constraint.get_target_table();
        if (tree_edges.count(std::make_pair(a, b)) == 0 &&
            tree_edges.count(std::make_pair(b, a)) == 0) {
            off_tree_conditions.push_back(constraint);
            continue;
        }
        if (constraint.is_point()) {
            tree_equalities.add(qualified(a, constraint.get_source_column()),
                                qualified(b, constraint.get_target_column()),
                                constraint.get_deviation1());
        }
        for (size_t i = 0; i < constraint.num_equalities(); i++) {
            tree_equalities.add(qualified(a, constraint.get_source_eq_columns()[i]),
                                qualified(b, constraint.get_target_eq_columns()[i]),
                                constraint.get_eq_offsets()[i]);
        }
    }

    // A condition between two tables that are not adjacent in the tree closes
    // a cycle in the join graph. It is harmless if the tree's equalities
    // already imply it (r.a = s.a AND s.a = u.a make r.a = u.a redundant):
    // then it can be left out without changing the result. Otherwise the
    // query cannot be evaluated; refuse it rather than silently returning the
    // result without that condition.
    for (const auto& constraint : off_tree_conditions) {
        const ImpliedStatus status = classify_against_tree(constraint, tree_equalities);
        if (status == ImpliedStatus::IMPLIED) {
            continue;
        }
        if (status == ImpliedStatus::CONTRADICTED) {
            throw std::runtime_error(
                "Contradictory join condition [" + constraint.to_string() +
                "]: it can never hold together with the query's other join conditions.");
        }
        const std::string& a = constraint.get_source_table();
        const std::string& b = constraint.get_target_table();
        throw std::runtime_error(
            "Unsupported join condition [" + constraint.to_string() + "]: tables '" + a +
            "' and '" + b + "' are not adjacent in the join tree (the condition closes a "
            "cycle in the join graph). Conditions between non-adjacent tables are not supported.");
    }

    // Validate that all tables are in the tree
    if (visited.size() != query.tables.size()) {
        std::cerr << "Warning: Not all tables connected in join tree. ";
        std::cerr << "Connected: " << visited.size() << "/" << query.tables.size() << std::endl;
        
        // Add disconnected tables as direct children of root (cross product)
        for (const auto& table_name : query.tables) {
            if (visited.find(table_name) == visited.end()) {
                if (tables.find(table_name) != tables.end()) {
                    // Create a default equality constraint (or could use cross product)
                    // For now, we'll skip adding disconnected tables
                    std::cerr << "Skipping disconnected table: " << table_name << std::endl;
                }
            }
        }
    }
    
    return root;
}

void JoinTreeBuilder::build_tree_recursive(
    JoinTreeNodePtr node,
    std::set<std::string>& visited_tables,
    const std::vector<std::string>& all_tables,
    const std::vector<JoinConstraint>& constraints,
    const std::map<std::string, Table>& table_map) {
    
    // Get all tables connected to current node's table
    std::vector<std::string> connected = get_connected_tables(node->get_table_name(), constraints);
    
    for (const auto& connected_table : connected) {
        // Skip if already visited
        if (visited_tables.find(connected_table) != visited_tables.end()) {
            continue;
        }
        
        // Find constraint between current node and connected table
        JoinConstraint constraint;
        bool found = find_constraint_between(
            node->get_table_name(), 
            connected_table, 
            constraints,
            constraint);
        
        if (!found) {
            continue;  // No direct constraint found
        }
        
        // Ensure constraint is from child's perspective (child is source)
        // If current node's table is the source, reverse the constraint
        if (constraint.get_source_table() == node->get_table_name()) {
            constraint = constraint.reverse();
        }
        
        // Check if connected table exists in table map
        if (table_map.find(connected_table) == table_map.end()) {
            std::cerr << "Warning: Table not found in map: " << connected_table << std::endl;
            continue;
        }
        
        // Create child node
        auto child = std::make_shared<JoinTreeNode>(
            connected_table, 
            table_map.at(connected_table));
        
        // Set join column on child (from constraint)
        child->set_join_column(constraint.get_source_column());
        
        // Add as child with constraint
        node->add_child(child, constraint);
        
        // Mark as visited
        visited_tables.insert(connected_table);
        
        // Recursively build subtree
        build_tree_recursive(child, visited_tables, all_tables, constraints, table_map);
    }
}

void JoinTreeBuilder::collect_tree_edges(
    JoinTreeNodePtr node,
    std::set<std::pair<std::string, std::string>>& edges) {

    for (const auto& child : node->get_children()) {
        edges.insert(std::make_pair(node->get_table_name(), child->get_table_name()));
        collect_tree_edges(child, edges);
    }
}

bool JoinTreeBuilder::find_constraint_between(
    const std::string& table1,
    const std::string& table2,
    const std::vector<JoinConstraint>& constraints,
    JoinConstraint& result) {
    
    for (const auto& constraint : constraints) {
        // Check if this constraint connects the two tables
        if ((constraint.get_source_table() == table1 && constraint.get_target_table() == table2) ||
            (constraint.get_source_table() == table2 && constraint.get_target_table() == table1)) {
            result = constraint;
            return true;
        }
    }
    
    return false;
}

std::vector<std::string> JoinTreeBuilder::get_connected_tables(
    const std::string& table,
    const std::vector<JoinConstraint>& constraints) {
    
    std::vector<std::string> result;
    
    for (const auto& constraint : constraints) {
        if (constraint.get_source_table() == table) {
            result.push_back(constraint.get_target_table());
        } else if (constraint.get_target_table() == table) {
            result.push_back(constraint.get_source_table());
        }
    }
    
    return result;
}

bool JoinTreeBuilder::validate_tree(
    JoinTreeNodePtr root,
    const ParsedQuery& query) {
    
    // Collect all table names in tree
    std::vector<std::string> tree_tables = root->get_all_table_names();
    std::set<std::string> tree_table_set(tree_tables.begin(), tree_tables.end());
    
    // Check if all query tables are in tree
    for (const auto& table : query.tables) {
        if (tree_table_set.find(table) == tree_table_set.end()) {
            std::cerr << "Table missing from tree: " << table << std::endl;
            return false;
        }
    }
    
    // Check if tree has extra tables
    if (tree_tables.size() != query.tables.size()) {
        std::cerr << "Tree has different number of tables than query" << std::endl;
        return false;
    }
    
    return true;
}