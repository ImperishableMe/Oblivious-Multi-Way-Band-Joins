#ifndef TYPES_COMMON_H
#define TYPES_COMMON_H

#include <stdint.h>
#include <stdbool.h>

// Entry type enumeration
typedef enum {
    EMPTY = 0,      // Uninitialized entry
    SOURCE = 1,     // Source table entry
    START = 2,      // Target start boundary
    END = 3,        // Target end boundary
    TARGET = 4      // Target table entry
} entry_type_t;

// Equality type for boundary conditions
typedef enum {
    NONE = 0,       // No equality constraint
    EQ = 1,         // Closed boundary (d or e)
    NEQ = 2         // Open boundary (< or >)
} equality_type_t;

#endif // TYPES_COMMON_H