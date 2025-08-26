#ifndef CONVERTERS_H
#define CONVERTERS_H

#include <vector>
#include <string>
#include <cstring>
#include "types.h"
#include "../enclave/enclave_types.h"

// Forward declarations
class Entry;
class Table;

/**
 * Convert C++ Entry to C entry_t structure
 * Copies all fields and converts std::vector to fixed arrays
 */
entry_t entry_to_entry_t(const Entry& entry);

/**
 * Convert C entry_t structure to C++ Entry
 * Copies all fields and converts fixed arrays to std::vector
 */
Entry entry_t_to_entry(const entry_t& c_entry);

/**
 * Convert entire Table to vector of entry_t structures
 * Used for batch processing in SGX enclave
 */
std::vector<entry_t> table_to_entry_t_vector(const Table& table);

/**
 * Convert vector of entry_t structures back to Table
 * Used after SGX processing to reconstruct C++ table
 */
Table entry_t_vector_to_table(const std::vector<entry_t>& entries);

/**
 * Helper function to convert std::string to char array
 * Ensures null termination and truncates if necessary
 */
void string_to_char_array(const std::string& str, char* arr, size_t max_len);

/**
 * Helper function to convert char array to std::string
 * Handles null termination properly
 */
std::string char_array_to_string(const char* arr, size_t max_len);

/**
 * Convert vector of strings to 2D char array
 * Used for column names conversion
 */
void strings_to_char_array_2d(const std::vector<std::string>& strings, 
                              char arr[][MAX_COLUMN_NAME_LEN], 
                              size_t max_strings);

/**
 * Convert 2D char array to vector of strings
 * Used for column names conversion
 */
std::vector<std::string> char_array_2d_to_strings(const char arr[][MAX_COLUMN_NAME_LEN], 
                                                  size_t num_strings);

/**
 * Convert vector of int32_t to fixed array
 * Used for attributes conversion
 */
void int32_to_array(const std::vector<int32_t>& vec, int32_t* arr, size_t max_size);

/**
 * Convert fixed array to vector of int32_t
 * Used for attributes conversion
 */
std::vector<int32_t> array_to_int32(const int32_t* arr, size_t size);

#endif // CONVERTERS_H