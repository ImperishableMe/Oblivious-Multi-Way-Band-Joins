#include "converters.h"
#include "../data_structures/entry.h"
#include "../data_structures/table.h"
#include <algorithm>
#include <cstring>

// Note: Entry conversion functions have been moved to Entry class methods
// Use Entry::to_entry_t() and Entry::from_entry_t() instead

// Note: Table::to_entry_t_vector() has been moved to Table class
// Use Table::to_entry_t_vector() instead

// Convert vector of entry_t structures back to Table
Table entry_t_vector_to_table(const std::vector<entry_t>& entries) {
    Table table;
    
    for (const auto& c_entry : entries) {
        Entry entry;
        entry.from_entry_t(c_entry);
        table.add_entry(entry);
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