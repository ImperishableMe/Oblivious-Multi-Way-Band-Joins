#ifndef JOIN_RESULT_COMPARATOR_H
#define JOIN_RESULT_COMPARATOR_H

#include <vector>
#include <string>
#include <set>
#include "../../app/types.h"

/**
 * JoinResultComparator - Compare join results for equivalence
 * 
 * Compares two Table objects to determine if they contain the same
 * set of rows, regardless of order. Used to validate join correctness
 * against ground truth.
 */
class JoinResultComparator {
private:
    double tolerance;  // For floating-point comparisons
    std::vector<std::string> differences;  // Detailed diff information
    
    /**
     * Convert an Entry to a normalized string for comparison
     * Sorts fields by name for consistent comparison
     */
    std::string entry_to_normalized_string(const Entry& entry) const;
    
    /**
     * Compare two entries for equality
     * Uses tolerance for numeric comparisons
     */
    bool entries_equal(const Entry& e1, const Entry& e2) const;
    
    /**
     * Get all column names from a table (union of all entries)
     */
    std::set<std::string> get_all_columns(const Table& table) const;
    
    /**
     * Normalize column names by removing table prefixes
     * e.g., "supplier.S_NATIONKEY" -> "S_NATIONKEY"
     */
    std::string normalize_column_name(const std::string& col_name) const;
    
    /**
     * Create a multiset of normalized entry strings for set comparison
     */
    std::multiset<std::string> table_to_multiset(const Table& table) const;
    
public:
    JoinResultComparator(double tol = 1e-9) : tolerance(tol) {}
    
    /**
     * Compare two tables for equivalence
     * Tables are equivalent if they contain the same multiset of rows
     * 
     * @param result1 First table
     * @param result2 Second table
     * @return true if tables are equivalent
     */
    bool are_equivalent(const Table& result1, const Table& result2);
    
    /**
     * Get detailed differences between tables
     * Only valid after calling are_equivalent()
     * 
     * @return Vector of difference descriptions
     */
    const std::vector<std::string>& get_differences() const { return differences; }
    
    /**
     * Clear stored differences
     */
    void clear_differences() { differences.clear(); }
    
    /**
     * Set tolerance for numeric comparisons
     */
    void set_tolerance(double tol) { tolerance = tol; }
    
    /**
     * Get current tolerance
     */
    double get_tolerance() const { return tolerance; }
    
    /**
     * Generate a detailed comparison report
     * 
     * @param result1 First table
     * @param result2 Second table
     * @return Human-readable comparison report
     */
    std::string generate_report(const Table& result1, const Table& result2);
};

#endif // JOIN_RESULT_COMPARATOR_H