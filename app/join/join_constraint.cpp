#include "join_constraint.h"
#include <sstream>
#include <iomanip>

std::string JoinConstraint::to_string() const {
    std::ostringstream oss;
    
    if (is_equality()) {
        oss << source_table << "." << source_column 
            << " = " 
            << target_table << "." << target_column;
    } else {
        oss << source_table << "." << source_column << " IN ";
        
        // Lower bound
        if (equality1 == EQ) {
            oss << "[";
        } else {
            oss << "(";
        }
        
        oss << target_table << "." << target_column;
        if (deviation1 != 0) {
            oss << std::showpos << deviation1 << std::noshowpos;
        }
        
        oss << ", ";
        
        // Upper bound
        oss << target_table << "." << target_column;
        if (deviation2 != 0) {
            oss << std::showpos << deviation2 << std::noshowpos;
        }
        
        if (equality2 == EQ) {
            oss << "]";
        } else {
            oss << ")";
        }
    }

    // Additional equalities of a composite constraint
    for (size_t i = 0; i < source_eq_columns.size(); i++) {
        oss << " AND " << source_table << "." << source_eq_columns[i]
            << " = "
            << target_table << "." << target_eq_columns[i];
        if (eq_offsets[i] != 0) {
            oss << std::showpos << eq_offsets[i] << std::noshowpos;
        }
    }

    return oss.str();
}