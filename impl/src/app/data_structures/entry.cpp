#include "entry.h"
#include <cstring>
#include <algorithm>
#include <sstream>
#include "../common/debug_util.h"

Entry::Entry() 
    : field_type(SOURCE), 
      equality_type(EQ), 
      is_encrypted(false),
      nonce(0),
      join_attr(0), 
      original_index(0), 
      local_mult(0), 
      final_mult(0),
      foreign_sum(0), 
      local_cumsum(0), 
      local_interval(0),
      foreign_interval(0), 
      local_weight(0),
      copy_index(0), 
      alignment_key(0),
      dst_idx(0),
      index(0) {
    // Initialize arrays
    memset(attributes, 0, sizeof(attributes));
    // column_names are automatically initialized as empty strings
}

Entry::Entry(const entry_t& c_entry) {
    from_entry_t(c_entry);
}

entry_t Entry::to_entry_t() const {
    // Removed TRACE logs to reduce debug output volume
    
    entry_t result;
    memset(&result, 0, sizeof(entry_t));
    
    // Copy basic fields
    result.field_type = field_type;
    result.equality_type = equality_type;
    result.is_encrypted = is_encrypted;
    result.nonce = nonce;
    result.join_attr = join_attr;
    result.original_index = original_index;
    result.local_mult = local_mult;
    result.final_mult = final_mult;
    result.foreign_sum = foreign_sum;
    result.local_cumsum = local_cumsum;
    result.local_interval = local_interval;
    result.foreign_interval = foreign_interval;
    result.local_weight = local_weight;
    result.copy_index = copy_index;
    result.alignment_key = alignment_key;
    result.dst_idx = dst_idx;
    result.index = index;
    
    // Copy all MAX_ATTRIBUTES - simpler and consistent
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        result.attributes[i] = attributes[i];
        strncpy(result.column_names[i], column_names[i].c_str(), MAX_COLUMN_NAME_LEN - 1);
        result.column_names[i][MAX_COLUMN_NAME_LEN - 1] = '\0';
    }
    
    return result;
}

void Entry::from_entry_t(const entry_t& c_entry) {
    // Removed TRACE logs to reduce debug output volume
    
    field_type = c_entry.field_type;
    equality_type = c_entry.equality_type;
    is_encrypted = c_entry.is_encrypted;
    nonce = c_entry.nonce;
    join_attr = c_entry.join_attr;
    original_index = c_entry.original_index;
    local_mult = c_entry.local_mult;
    final_mult = c_entry.final_mult;
    foreign_sum = c_entry.foreign_sum;
    local_cumsum = c_entry.local_cumsum;
    local_interval = c_entry.local_interval;
    foreign_interval = c_entry.foreign_interval;
    local_weight = c_entry.local_weight;
    copy_index = c_entry.copy_index;
    alignment_key = c_entry.alignment_key;
    dst_idx = c_entry.dst_idx;
    index = c_entry.index;
    
    // Copy all MAX_ATTRIBUTES - no need to figure out actual size
    // Empty entries will just have empty strings and zeros
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        column_names[i] = std::string(c_entry.column_names[i]);
        attributes[i] = c_entry.attributes[i];
    }
}

void Entry::from_entry_t(const entry_t& c_entry, const std::vector<std::string>& schema) {
    // Copy all metadata fields
    field_type = c_entry.field_type;
    equality_type = c_entry.equality_type;
    is_encrypted = c_entry.is_encrypted;
    nonce = c_entry.nonce;
    join_attr = c_entry.join_attr;
    original_index = c_entry.original_index;
    local_mult = c_entry.local_mult;
    final_mult = c_entry.final_mult;
    foreign_sum = c_entry.foreign_sum;
    local_cumsum = c_entry.local_cumsum;
    local_interval = c_entry.local_interval;
    foreign_interval = c_entry.foreign_interval;
    local_weight = c_entry.local_weight;
    copy_index = c_entry.copy_index;
    alignment_key = c_entry.alignment_key;
    dst_idx = c_entry.dst_idx;
    index = c_entry.index;
    
    // Copy all MAX_ATTRIBUTES from c_entry
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        attributes[i] = c_entry.attributes[i];
    }
    
    // Set column names from schema
    size_t schema_size = std::min(schema.size(), (size_t)MAX_ATTRIBUTES);
    for (size_t i = 0; i < schema_size; i++) {
        column_names[i] = schema[i];
    }
    // Clear remaining column names
    for (size_t i = schema_size; i < MAX_ATTRIBUTES; i++) {
        column_names[i].clear();
    }
}

void Entry::clear() {
    *this = Entry();  // Reset to default values
}

int32_t Entry::get_attribute(const std::string& column_name) const {
    // Find the column index - check all MAX_ATTRIBUTES
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (!column_names[i].empty() && column_names[i] == column_name) {
            return attributes[i];
        }
    }
    return 0;  // Return 0 if column not found
}

bool Entry::has_attribute(const std::string& column_name) const {
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (!column_names[i].empty() && column_names[i] == column_name) {
            return true;
        }
    }
    return false;
}

void Entry::set_attribute(const std::string& column_name, int32_t value) {
    // Find the column index
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (!column_names[i].empty() && column_names[i] == column_name) {
            attributes[i] = value;
            return;
        }
    }
    // If column not found, add it
    add_attribute(column_name, value);
}

void Entry::add_attribute(const std::string& column_name, int32_t value) {
    // Find first empty slot
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (column_names[i].empty()) {
            column_names[i] = column_name;
            attributes[i] = value;
            return;
        }
    }
}

std::map<std::string, int32_t> Entry::get_attributes_map() const {
    std::map<std::string, int32_t> result;
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (!column_names[i].empty()) {
            result[column_names[i]] = attributes[i];
        }
    }
    return result;
}

bool Entry::operator<(const Entry& other) const {
    return join_attr < other.join_attr;
}

bool Entry::operator==(const Entry& other) const {
    return join_attr == other.join_attr &&
           field_type == other.field_type &&
           original_index == other.original_index;
}

std::string Entry::to_string() const {
    std::stringstream ss;
    ss << "Entry{type=" << field_type 
       << ", join_attr=" << join_attr
       << ", local_mult=" << local_mult
       << ", final_mult=" << final_mult
       << ", attrs=[";
    bool first = true;
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (!column_names[i].empty()) {
            if (!first) ss << ", ";
            ss << column_names[i] << "=" << attributes[i];
            first = false;
        }
    }
    ss << "]}";
    return ss.str();
}