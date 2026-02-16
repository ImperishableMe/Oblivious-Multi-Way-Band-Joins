#ifndef APP_IO_ENTRY_H
#define APP_IO_ENTRY_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include "../../common/enclave_types.h"
#include "../../common/entry_utils.h"

/**
 * IO_Entry - A lightweight entry type for I/O operations only
 *
 * This class provides a dynamic attribute vector for I/O operations,
 * avoiding the fixed MAX_ATTRIBUTES size requirement of Entry.
 * Used only by Table for save/load operations.
 */
class IO_Entry {
public:
    // Only store the actual data needed for I/O
    std::vector<int32_t> attributes;
    std::vector<std::string> column_names;

    // Join attribute for convenience
    int32_t join_attr;

    // Constructors
    IO_Entry() : join_attr(0) {}

    // Conversion from regular Entry (without column_names)
    IO_Entry(const Entry& entry, const std::vector<std::string>& schema) {
        // Copy attributes based on schema size
        for (size_t i = 0; i < schema.size() && i < MAX_ATTRIBUTES; i++) {
            attributes.push_back(entry.attributes[i]);
            column_names.push_back(schema[i]);
        }
        join_attr = entry.join_attr;
    }
    
    Entry to_entry() const {
        Entry entry;
        memset(&entry, 0, sizeof(entry_t));

        // Copy data to fixed arrays
        size_t copy_count = std::min(attributes.size(), (size_t)MAX_ATTRIBUTES);
        for (size_t i = 0; i < copy_count; i++) {
            entry.attributes[i] = attributes[i];
        }

        entry.join_attr = join_attr;

        return entry;
    }
};

#endif // APP_IO_ENTRY_H