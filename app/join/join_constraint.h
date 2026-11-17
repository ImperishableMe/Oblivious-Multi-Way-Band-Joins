#ifndef JOIN_CONSTRAINT_H
#define JOIN_CONSTRAINT_H

#include <string>
#include <vector>
#include <stdint.h>
#include "../../common/enclave_types.h"

/**
 * JoinConstraint Class
 *
 * Represents a join condition between two tables in a band join.
 * Encodes the relationship:
 *   source.join_attr IN [target.join_attr + deviation1, target.join_attr + deviation2]
 *
 * The deviations represent offsets applied to the target table's join attribute
 * to define the range that matches the source table's join attribute.
 *
 * Examples:
 * - Equality join: deviation1=0, equality1=EQ, deviation2=0, equality2=EQ
 * - Band join [target-100, target+1000]: deviation1=-100, equality1=EQ, deviation2=1000, equality2=EQ
 * - Open interval (target, target+10): deviation1=0, equality1=NEQ, deviation2=10, equality2=NEQ
 *
 * Composite constraints
 * ---------------------
 * Two tables may be joined on several conditions at once: any number of
 * equalities plus at most one range. The range (or, if there is none, one of
 * the equalities) is the condition above; the remaining equalities are kept as
 *   source.source_eq_columns[i] = target.target_eq_columns[i] + eq_offsets[i]
 * Together they form one composite join key - equality columns first, the
 * range column last - under which the rows matching a given row are still a
 * single contiguous interval, so the band-join machinery applies unchanged.
 */

// Simple struct to hold constraint parameters for algorithm use
struct ConstraintParam {
    int32_t deviation1;
    equality_type_t equality1;
    int32_t deviation2;
    equality_type_t equality2;
};

class JoinConstraint {
private:
    std::string source_table;   // Child table in join tree
    std::string target_table;   // Parent table in join tree
    std::string source_column;  // Join column in source table
    std::string target_column;  // Join column in target table
    int32_t deviation1;          // Lower bound offset
    equality_type_t equality1;   // EQ or NEQ for lower bound
    int32_t deviation2;          // Upper bound offset
    equality_type_t equality2;   // EQ or NEQ for upper bound

    // Additional equality conditions of a composite constraint:
    //   source.source_eq_columns[i] = target.target_eq_columns[i] + eq_offsets[i]
    std::vector<std::string> source_eq_columns;
    std::vector<std::string> target_eq_columns;
    std::vector<int32_t> eq_offsets;

public:
    // Default constructor
    JoinConstraint() : deviation1(0), equality1(NONE), deviation2(0), equality2(NONE) {}
    
    // Full constructor
    JoinConstraint(const std::string& src_table, const std::string& src_col,
                   const std::string& tgt_table, const std::string& tgt_col,
                   int32_t dev1, equality_type_t eq1,
                   int32_t dev2, equality_type_t eq2)
        : source_table(src_table), target_table(tgt_table),
          source_column(src_col), target_column(tgt_col),
          deviation1(dev1), equality1(eq1),
          deviation2(dev2), equality2(eq2) {}
    
    // Getters
    const std::string& get_source_table() const { return source_table; }
    const std::string& get_target_table() const { return target_table; }
    const std::string& get_source_column() const { return source_column; }
    const std::string& get_target_column() const { return target_column; }
    int32_t get_deviation1() const { return deviation1; }
    equality_type_t get_equality1() const { return equality1; }
    int32_t get_deviation2() const { return deviation2; }
    equality_type_t get_equality2() const { return equality2; }
    
    // Get constraint parameters for algorithm
    ConstraintParam get_params() const {
        return {deviation1, equality1, deviation2, equality2};
    }

    // Additional equality conditions (composite constraints)
    //   source.src_col = target.tgt_col + offset
    void add_equality(const std::string& src_col, const std::string& tgt_col, int32_t offset = 0) {
        source_eq_columns.push_back(src_col);
        target_eq_columns.push_back(tgt_col);
        eq_offsets.push_back(offset);
    }
    size_t num_equalities() const { return source_eq_columns.size(); }
    const std::vector<std::string>& get_source_eq_columns() const { return source_eq_columns; }
    const std::vector<std::string>& get_target_eq_columns() const { return target_eq_columns; }
    const std::vector<int32_t>& get_eq_offsets() const { return eq_offsets; }

    // Reverse the constraint (swap source/target)
    // When reversing: source becomes target, target becomes source
    // Deviations are negated and bounds are swapped
    // Example: if target + 100 >= source, then source - 100 <= target
    // Additional equalities swap sides too: s = t + c becomes t = s - c
    JoinConstraint reverse() const {
        JoinConstraint reversed(
            target_table, target_column,  // Swap tables
            source_table, source_column,
            -deviation2, equality2,       // Upper becomes lower, negated
            -deviation1, equality1        // Lower becomes upper, negated
        );
        for (size_t i = 0; i < source_eq_columns.size(); i++) {
            reversed.add_equality(target_eq_columns[i], source_eq_columns[i], -eq_offsets[i]);
        }
        return reversed;
    }
    
    // Factory method for equality join
    static JoinConstraint equality(const std::string& src_table, const std::string& src_col,
                                  const std::string& tgt_table, const std::string& tgt_col) {
        return JoinConstraint(src_table, src_col, tgt_table, tgt_col,
                            0, EQ, 0, EQ);
    }
    
    // Factory method for band join with closed intervals
    static JoinConstraint band(const std::string& src_table, const std::string& src_col,
                               const std::string& tgt_table, const std::string& tgt_col,
                               int32_t lower, int32_t upper,
                               bool lower_inclusive = true, bool upper_inclusive = true) {
        equality_type_t lower_eq = lower_inclusive ? EQ : NEQ;
        equality_type_t upper_eq = upper_inclusive ? EQ : NEQ;
        return JoinConstraint(src_table, src_col, tgt_table, tgt_col,
                            lower, lower_eq, upper, upper_eq);
    }
    
    // Check if this is an equality join
    bool is_equality() const {
        return deviation1 == 0 && deviation2 == 0 &&
               equality1 == EQ && equality2 == EQ;
    }

    // Check if the main condition pins the source to a single value,
    // i.e. it is an equality possibly with a constant offset:
    //   source.col = target.col + deviation1
    bool is_point() const {
        return deviation1 == deviation2 && equality1 == EQ && equality2 == EQ;
    }
    
    // Check if constraint is valid
    bool is_valid() const {
        // Lower bound should not exceed upper bound
        // Account for open/closed intervals
        if (deviation1 > deviation2) return false;
        if (deviation1 == deviation2) {
            // For same deviation, both must be EQ for equality join
            // or we have an empty interval
            if (equality1 == NEQ || equality2 == NEQ) {
                return deviation1 == 0 && deviation2 == 0 && 
                       equality1 == EQ && equality2 == EQ;
            }
        }
        return true;
    }
    
    // String representation for debugging
    std::string to_string() const;
};

#endif // JOIN_CONSTRAINT_H