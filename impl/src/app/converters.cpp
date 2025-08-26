#include "converters.h"
#include <algorithm>
#include <cstring>

// Convert C++ Entry to C entry_t structure
entry_t entry_to_entry_t(const Entry& entry) {
    entry_t c_entry;
    
    // Copy metadata fields
    c_entry.field_type = entry.field_type;
    c_entry.equality_type = entry.equality_type;
    c_entry.is_encrypted = entry.is_encrypted;
    c_entry.nonce = entry.nonce;
    
    // Copy join attribute
    c_entry.join_attr = entry.join_attr;
    
    // Copy persistent metadata
    c_entry.original_index = entry.original_index;
    c_entry.local_mult = entry.local_mult;
    c_entry.final_mult = entry.final_mult;
    c_entry.foreign_sum = entry.foreign_sum;
    
    // Copy temporary metadata
    c_entry.local_cumsum = entry.local_cumsum;
    c_entry.local_interval = entry.local_interval;
    c_entry.foreign_cumsum = entry.foreign_cumsum;
    c_entry.foreign_interval = entry.foreign_interval;
    c_entry.local_weight = entry.local_weight;
    
    // Copy expansion metadata
    c_entry.copy_index = entry.copy_index;
    c_entry.alignment_key = entry.alignment_key;
    
    // Convert attributes vector to array
    int32_to_array(entry.attributes, c_entry.attributes, MAX_ATTRIBUTES);
    
    // Convert column names vector to 2D char array
    strings_to_char_array_2d(entry.column_names, c_entry.column_names, MAX_ATTRIBUTES);
    
    return c_entry;
}

// Convert C entry_t structure to C++ Entry
Entry entry_t_to_entry(const entry_t& c_entry) {
    Entry entry;
    
    // Copy metadata fields
    entry.field_type = c_entry.field_type;
    entry.equality_type = c_entry.equality_type;
    entry.is_encrypted = c_entry.is_encrypted;
    entry.nonce = c_entry.nonce;
    
    // Copy join attribute
    entry.join_attr = c_entry.join_attr;
    
    // Copy persistent metadata
    entry.original_index = c_entry.original_index;
    entry.local_mult = c_entry.local_mult;
    entry.final_mult = c_entry.final_mult;
    entry.foreign_sum = c_entry.foreign_sum;
    
    // Copy temporary metadata
    entry.local_cumsum = c_entry.local_cumsum;
    entry.local_interval = c_entry.local_interval;
    entry.foreign_cumsum = c_entry.foreign_cumsum;
    entry.foreign_interval = c_entry.foreign_interval;
    entry.local_weight = c_entry.local_weight;
    
    // Copy expansion metadata
    entry.copy_index = c_entry.copy_index;
    entry.alignment_key = c_entry.alignment_key;
    
    // Convert attributes array to vector
    // Count actual number of attributes (non-zero values)
    size_t num_attrs = 0;
    for (size_t i = 0; i < MAX_ATTRIBUTES; i++) {
        if (c_entry.attributes[i] != 0 || i < entry.column_names.size()) {
            num_attrs = i + 1;
        }
    }
    entry.attributes = array_to_int32(c_entry.attributes, num_attrs);
    
    // Convert column names 2D char array to vector
    // Count actual number of column names
    size_t num_cols = 0;
    for (size_t i = 0; i < MAX_ATTRIBUTES; i++) {
        if (strlen(c_entry.column_names[i]) > 0) {
            num_cols = i + 1;
        }
    }
    entry.column_names = char_array_2d_to_strings(c_entry.column_names, num_cols);
    
    return entry;
}

// Convert entire Table to vector of entry_t structures
std::vector<entry_t> table_to_entry_t_vector(const Table& table) {
    std::vector<entry_t> c_entries;
    c_entries.reserve(table.size());
    
    for (size_t i = 0; i < table.size(); i++) {
        c_entries.push_back(entry_to_entry_t(table.get_entry(i)));
    }
    
    return c_entries;
}

// Convert vector of entry_t structures back to Table
Table entry_t_vector_to_table(const std::vector<entry_t>& entries) {
    Table table;
    
    for (const auto& c_entry : entries) {
        table.add_entry(entry_t_to_entry(c_entry));
    }
    
    return table;
}

// Helper function to convert std::string to char array
void string_to_char_array(const std::string& str, char* arr, size_t max_len) {
    memset(arr, 0, max_len);
    size_t copy_len = std::min(str.length(), max_len - 1);
    memcpy(arr, str.c_str(), copy_len);
    arr[copy_len] = '\0';
}

// Helper function to convert char array to std::string
std::string char_array_to_string(const char* arr, size_t max_len) {
    // Find actual string length (up to null terminator or max_len)
    size_t len = 0;
    while (len < max_len && arr[len] != '\0') {
        len++;
    }
    return std::string(arr, len);
}

// Convert vector of strings to 2D char array
void strings_to_char_array_2d(const std::vector<std::string>& strings, 
                              char arr[][MAX_COLUMN_NAME_LEN], 
                              size_t max_strings) {
    // Clear the array first
    memset(arr, 0, max_strings * MAX_COLUMN_NAME_LEN);
    
    size_t num_to_copy = std::min(strings.size(), max_strings);
    for (size_t i = 0; i < num_to_copy; i++) {
        string_to_char_array(strings[i], arr[i], MAX_COLUMN_NAME_LEN);
    }
}

// Convert 2D char array to vector of strings
std::vector<std::string> char_array_2d_to_strings(const char arr[][MAX_COLUMN_NAME_LEN], 
                                                  size_t num_strings) {
    std::vector<std::string> strings;
    strings.reserve(num_strings);
    
    for (size_t i = 0; i < num_strings; i++) {
        strings.push_back(char_array_to_string(arr[i], MAX_COLUMN_NAME_LEN));
    }
    
    return strings;
}

// Convert vector of int32_t to fixed array
void int32_to_array(const std::vector<int32_t>& vec, int32_t* arr, size_t max_size) {
    // Clear the array first
    memset(arr, 0, max_size * sizeof(int32_t));
    
    size_t num_to_copy = std::min(vec.size(), max_size);
    for (size_t i = 0; i < num_to_copy; i++) {
        arr[i] = vec[i];
    }
}

// Convert fixed array to vector of int32_t
std::vector<int32_t> array_to_int32(const int32_t* arr, size_t size) {
    std::vector<int32_t> vec;
    vec.reserve(size);
    
    for (size_t i = 0; i < size; i++) {
        vec.push_back(arr[i]);
    }
    
    return vec;
}