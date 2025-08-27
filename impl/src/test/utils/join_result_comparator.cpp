#include "join_result_comparator.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

std::string JoinResultComparator::normalize_column_name(const std::string& col_name) const {
    size_t dot_pos = col_name.find('.');
    if (dot_pos != std::string::npos) {
        return col_name.substr(dot_pos + 1);
    }
    return col_name;
}

std::string JoinResultComparator::entry_to_normalized_string(const Entry& entry) const {
    // Get all attributes and sort by normalized column name
    std::map<std::string, int32_t> sorted_fields;
    
    auto attrs = entry.get_attributes_map();
    for (const auto& [col_name, value] : attrs) {
        std::string norm_name = normalize_column_name(col_name);
        sorted_fields[norm_name] = value;
    }
    
    // Build string representation
    std::stringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& [col, val] : sorted_fields) {
        if (!first) ss << ",";
        first = false;
        ss << col << ":" << val;
    }
    ss << "}";
    
    return ss.str();
}

bool JoinResultComparator::entries_equal(const Entry& e1, const Entry& e2) const {
    // Get normalized field maps
    std::map<std::string, int32_t> fields1, fields2;
    
    auto attrs1 = e1.get_attributes_map();
    for (const auto& [col, val] : attrs1) {
        fields1[normalize_column_name(col)] = val;
    }
    
    auto attrs2 = e2.get_attributes_map();
    for (const auto& [col, val] : attrs2) {
        fields2[normalize_column_name(col)] = val;
    }
    
    // Check same number of fields
    if (fields1.size() != fields2.size()) {
        return false;
    }
    
    // Check each field
    for (const auto& [col, val1] : fields1) {
        auto it = fields2.find(col);
        if (it == fields2.end()) {
            return false;  // Column missing in e2
        }
        
        // Compare values with tolerance
        if (std::abs(val1 - it->second) > tolerance) {
            return false;
        }
    }
    
    return true;
}

std::set<std::string> JoinResultComparator::get_all_columns(const Table& table) const {
    std::set<std::string> columns;
    
    for (size_t i = 0; i < table.size(); i++) {
        auto attrs = table[i].get_attributes_map();
        for (const auto& [col, val] : attrs) {
            columns.insert(normalize_column_name(col));
        }
    }
    
    return columns;
}

std::multiset<std::string> JoinResultComparator::table_to_multiset(const Table& table) const {
    std::multiset<std::string> result;
    
    for (size_t i = 0; i < table.size(); i++) {
        result.insert(entry_to_normalized_string(table[i]));
    }
    
    return result;
}

bool JoinResultComparator::are_equivalent(const Table& result1, const Table& result2) {
    clear_differences();
    
    // Quick check: same number of rows
    if (result1.size() != result2.size()) {
        differences.push_back("Row count mismatch: " + 
                            std::to_string(result1.size()) + " vs " + 
                            std::to_string(result2.size()));
        return false;
    }
    
    // Check column sets
    auto cols1 = get_all_columns(result1);
    auto cols2 = get_all_columns(result2);
    
    if (cols1 != cols2) {
        // Find column differences
        std::vector<std::string> only_in_1, only_in_2;
        
        std::set_difference(cols1.begin(), cols1.end(),
                          cols2.begin(), cols2.end(),
                          std::back_inserter(only_in_1));
        
        std::set_difference(cols2.begin(), cols2.end(),
                          cols1.begin(), cols1.end(),
                          std::back_inserter(only_in_2));
        
        if (!only_in_1.empty()) {
            std::stringstream ss;
            ss << "Columns only in result1: ";
            for (const auto& col : only_in_1) ss << col << " ";
            differences.push_back(ss.str());
        }
        
        if (!only_in_2.empty()) {
            std::stringstream ss;
            ss << "Columns only in result2: ";
            for (const auto& col : only_in_2) ss << col << " ";
            differences.push_back(ss.str());
        }
        
        return false;
    }
    
    // Convert to multisets for order-independent comparison
    auto set1 = table_to_multiset(result1);
    auto set2 = table_to_multiset(result2);
    
    if (set1 != set2) {
        // Find row differences
        std::vector<std::string> only_in_1, only_in_2;
        
        std::set_difference(set1.begin(), set1.end(),
                          set2.begin(), set2.end(),
                          std::back_inserter(only_in_1));
        
        std::set_difference(set2.begin(), set2.end(),
                          set1.begin(), set1.end(),
                          std::back_inserter(only_in_2));
        
        if (!only_in_1.empty()) {
            differences.push_back("Rows only in result1: " + std::to_string(only_in_1.size()));
            for (size_t i = 0; i < std::min(size_t(5), only_in_1.size()); i++) {
                differences.push_back("  " + only_in_1[i]);
            }
            if (only_in_1.size() > 5) {
                differences.push_back("  ... and " + 
                                    std::to_string(only_in_1.size() - 5) + " more");
            }
        }
        
        if (!only_in_2.empty()) {
            differences.push_back("Rows only in result2: " + std::to_string(only_in_2.size()));
            for (size_t i = 0; i < std::min(size_t(5), only_in_2.size()); i++) {
                differences.push_back("  " + only_in_2[i]);
            }
            if (only_in_2.size() > 5) {
                differences.push_back("  ... and " + 
                                    std::to_string(only_in_2.size() - 5) + " more");
            }
        }
        
        return false;
    }
    
    return true;
}

std::string JoinResultComparator::generate_report(const Table& result1, const Table& result2) {
    std::stringstream report;
    
    report << "=== Join Result Comparison Report ===" << std::endl;
    report << "Result 1: " << result1.size() << " rows" << std::endl;
    report << "Result 2: " << result2.size() << " rows" << std::endl;
    
    bool equivalent = are_equivalent(result1, result2);
    
    if (equivalent) {
        report << "✓ Results are EQUIVALENT" << std::endl;
    } else {
        report << "✗ Results are NOT equivalent" << std::endl;
        report << "\nDifferences found:" << std::endl;
        for (const auto& diff : differences) {
            report << "  " << diff << std::endl;
        }
    }
    
    // Column summary
    auto cols1 = get_all_columns(result1);
    auto cols2 = get_all_columns(result2);
    
    report << "\nColumn Summary:" << std::endl;
    report << "  Result 1 columns (" << cols1.size() << "): ";
    for (const auto& col : cols1) {
        report << col << " ";
    }
    report << std::endl;
    
    report << "  Result 2 columns (" << cols2.size() << "): ";
    for (const auto& col : cols2) {
        report << col << " ";
    }
    report << std::endl;
    
    return report.str();
}