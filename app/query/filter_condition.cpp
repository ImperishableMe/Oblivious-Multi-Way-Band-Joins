#include "filter_condition.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

// Helper function to trim whitespace
static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

bool FilterCondition::parse(const std::string& filter_str, FilterCondition& out) {
    std::string s = trim(filter_str);
    if (s.empty()) return false;

    // Find the operator position
    // Supported operators: >=, <=, !=, =, >, <
    size_t op_pos = std::string::npos;
    size_t op_len = 0;
    std::string op;

    // Check for two-character operators first
    size_t pos_ge = s.find(">=");
    size_t pos_le = s.find("<=");
    size_t pos_ne = s.find("!=");
    size_t pos_ne2 = s.find("<>");

    if (pos_ge != std::string::npos && (op_pos == std::string::npos || pos_ge < op_pos)) {
        op_pos = pos_ge;
        op_len = 2;
        op = ">=";
    }
    if (pos_le != std::string::npos && (op_pos == std::string::npos || pos_le < op_pos)) {
        op_pos = pos_le;
        op_len = 2;
        op = "<=";
    }
    if (pos_ne != std::string::npos && (op_pos == std::string::npos || pos_ne < op_pos)) {
        op_pos = pos_ne;
        op_len = 2;
        op = "!=";
    }
    if (pos_ne2 != std::string::npos && (op_pos == std::string::npos || pos_ne2 < op_pos)) {
        op_pos = pos_ne2;
        op_len = 2;
        op = "!=";  // Normalize <> to !=
    }

    // If no two-char operator found, check single-char operators
    if (op_pos == std::string::npos) {
        size_t pos_eq = s.find('=');
        size_t pos_gt = s.find('>');
        size_t pos_lt = s.find('<');

        if (pos_eq != std::string::npos && (op_pos == std::string::npos || pos_eq < op_pos)) {
            op_pos = pos_eq;
            op_len = 1;
            op = "=";
        }
        if (pos_gt != std::string::npos && (op_pos == std::string::npos || pos_gt < op_pos)) {
            op_pos = pos_gt;
            op_len = 1;
            op = ">";
        }
        if (pos_lt != std::string::npos && (op_pos == std::string::npos || pos_lt < op_pos)) {
            op_pos = pos_lt;
            op_len = 1;
            op = "<";
        }
    }

    if (op_pos == std::string::npos) {
        std::cerr << "Filter parse error: No operator found in '" << filter_str << "'" << std::endl;
        return false;
    }

    // Extract left side (table.column) and right side (value)
    std::string left = trim(s.substr(0, op_pos));
    std::string right = trim(s.substr(op_pos + op_len));

    // Parse left side: table_alias.column_name
    size_t dot_pos = left.find('.');
    if (dot_pos == std::string::npos) {
        std::cerr << "Filter parse error: Expected table.column format in '" << left << "'" << std::endl;
        return false;
    }

    out.table_alias = trim(left.substr(0, dot_pos));
    out.column_name = trim(left.substr(dot_pos + 1));
    out.op = op;

    // Parse right side: integer value
    try {
        out.value = std::stoi(right);
    } catch (const std::exception& e) {
        std::cerr << "Filter parse error: Expected integer value, got '" << right << "'" << std::endl;
        return false;
    }

    return true;
}

int32_t FilterCondition::evaluate(int32_t attr_value) const {
    // Oblivious evaluation - returns 1 if matches, 0 if not
    // No early returns or branching based on data

    int32_t result = 0;

    if (op == "=") {
        result = (attr_value == value) ? 1 : 0;
    } else if (op == ">") {
        result = (attr_value > value) ? 1 : 0;
    } else if (op == ">=") {
        result = (attr_value >= value) ? 1 : 0;
    } else if (op == "<") {
        result = (attr_value < value) ? 1 : 0;
    } else if (op == "<=") {
        result = (attr_value <= value) ? 1 : 0;
    } else if (op == "!=") {
        result = (attr_value != value) ? 1 : 0;
    }

    return result;
}

std::string FilterCondition::to_string() const {
    std::stringstream ss;
    ss << table_alias << "." << column_name << " " << op << " " << value;
    return ss.str();
}

// ============================================================================
// FilterApplicator Implementation
// ============================================================================

void FilterApplicator::apply_filter(Table& table, const FilterCondition& filter) {
    // Check if column exists in table schema
    if (!table.has_column(filter.column_name)) {
        std::cerr << "Warning: Column '" << filter.column_name
                  << "' not found in table '" << table.get_table_name()
                  << "'. Filter skipped." << std::endl;
        return;
    }

    size_t col_idx = table.get_column_index(filter.column_name);

    // Apply filter obliviously to ALL entries
    // Same operations performed regardless of filter result
    for (size_t i = 0; i < table.size(); i++) {
        Entry& entry = table[i];

        // Get the attribute value for this column
        int32_t attr_value = entry.attributes[col_idx];

        // Evaluate filter: returns 1 if matches, 0 if not
        int32_t matches = filter.evaluate(attr_value);

        // Oblivious update: local_mult = matches * local_mult
        // If matches=1, local_mult unchanged
        // If matches=0, local_mult becomes 0
        entry.local_mult = matches * entry.local_mult;
    }
}

void FilterApplicator::apply_filters(
    std::map<std::string, Table>& aliased_tables,
    const std::vector<std::string>& filter_strings
) {
    for (const auto& filter_str : filter_strings) {
        FilterCondition filter;

        if (!FilterCondition::parse(filter_str, filter)) {
            std::cerr << "Warning: Failed to parse filter condition: '"
                      << filter_str << "'" << std::endl;
            continue;
        }

        // Find the table this filter applies to
        auto it = aliased_tables.find(filter.table_alias);
        if (it == aliased_tables.end()) {
            std::cerr << "Warning: Table alias '" << filter.table_alias
                      << "' not found. Filter skipped: " << filter.to_string() << std::endl;
            continue;
        }

        // Apply the filter to the table
        std::cout << "Applying filter: " << filter.to_string()
                  << " to table alias '" << filter.table_alias << "'" << std::endl;

        apply_filter(it->second, filter);
    }
}
