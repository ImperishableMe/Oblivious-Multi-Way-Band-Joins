#ifndef ENTRY_UTILS_H
#define ENTRY_UTILS_H

#include "enclave_types.h"
#include <cstring>
#include <string>
#include <sstream>

// Typedef: Entry is now just entry_t (no wrapper class)
using Entry = entry_t;

// Initialize an entry to default values (replaces Entry() constructor)
inline void entry_clear(entry_t& e) {
    memset(&e, 0, sizeof(entry_t));
    e.field_type = SOURCE;
    e.equality_type = EQ;
}

// Create a default-initialized entry (replaces Entry() constructor)
inline entry_t make_entry() {
    entry_t e;
    entry_clear(e);
    return e;
}

// Comparison operators (replaces Entry::operator< and Entry::operator==)
inline bool operator<(const entry_t& a, const entry_t& b) {
    return a.join_attr < b.join_attr;
}

inline bool operator==(const entry_t& a, const entry_t& b) {
    return a.join_attr == b.join_attr &&
           a.field_type == b.field_type &&
           a.original_index == b.original_index;
}

// Debug string representation (replaces Entry::to_string())
inline std::string entry_to_string(const entry_t& e) {
    std::stringstream ss;
    ss << "Entry{type=" << e.field_type
       << ", join_attr=" << e.join_attr
       << ", local_mult=" << e.local_mult
       << ", final_mult=" << e.final_mult
       << ", attrs=[";
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (i > 0) ss << ", ";
        ss << e.attributes[i];
    }
    ss << "]}";
    return ss.str();
}

#endif // ENTRY_UTILS_H
