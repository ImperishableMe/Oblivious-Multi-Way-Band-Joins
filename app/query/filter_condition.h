#ifndef FILTER_CONDITION_H
#define FILTER_CONDITION_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "../data_structures/table.h"

/**
 * FilterCondition - Represents a parsed WHERE clause filter predicate
 *
 * Supports equality filters of the form: table_alias.column_name = value
 * Example: "a.balance = 500000" or "t.amount = 50000"
 */
struct FilterCondition {
    std::string table_alias;    // e.g., "a" from "a.balance"
    std::string column_name;    // e.g., "balance" from "a.balance"
    std::string op;             // Comparison operator: "=", ">", ">=", "<", "<=", "!="
    int32_t value;              // The literal value to compare against

    /**
     * Parse a filter string into a FilterCondition
     * @param filter_str Raw filter string like "a.balance > 500000"
     * @param out Output FilterCondition struct
     * @return true if parsing succeeded, false otherwise
     */
    static bool parse(const std::string& filter_str, FilterCondition& out);

    /**
     * Evaluate the filter condition against a value
     * Returns 1 if the condition is satisfied, 0 otherwise
     * This is designed for oblivious computation (no branching)
     * @param attr_value The attribute value to test
     * @return 1 if matches, 0 if not
     */
    int32_t evaluate(int32_t attr_value) const;

    /**
     * String representation for debugging
     */
    std::string to_string() const;
};

/**
 * FilterApplicator - Applies filter conditions to tables obliviously
 *
 * The filtering is done by setting local_mult = 0 for entries that
 * don't match the filter. This is oblivious because:
 * 1. Every entry is processed (no skipping)
 * 2. The same operations are performed regardless of filter result
 * 3. Multiplication by 0 or 1 is data-independent in terms of access pattern
 */
class FilterApplicator {
public:
    /**
     * Apply a single filter to a table obliviously
     * Sets local_mult = 0 for entries that don't satisfy the filter
     *
     * @param table The table to filter (modified in place)
     * @param filter The filter condition to apply
     */
    static void apply_filter(Table& table, const FilterCondition& filter);

    /**
     * Apply multiple filters to aliased tables
     * Parses filter strings and applies each to the appropriate table
     *
     * @param aliased_tables Map of alias -> Table
     * @param filter_strings Raw filter condition strings from parser
     */
    static void apply_filters(
        std::map<std::string, Table>& aliased_tables,
        const std::vector<std::string>& filter_strings
    );
};

#endif // FILTER_CONDITION_H
