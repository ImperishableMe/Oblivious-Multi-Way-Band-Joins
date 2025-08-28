#ifndef APP_ENTRY_H
#define APP_ENTRY_H

#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include "../../common/constants.h"
#include "../../common/types_common.h"
#include "../../enclave/enclave_types.h"

/**
 * Entry Class - Represents a single row/tuple in a table
 * 
 * Encapsulates all metadata and attributes for oblivious multi-way band join processing.
 * Supports conversion between C++ and C structs for SGX enclave communication.
 */
class Entry {
public:
    // Entry metadata (using int32_t for consistency with enclave struct)
    int32_t field_type;      // entry_type_t
    int32_t equality_type;   // equality_type_t
    bool is_encrypted;
    
    // Encryption nonce for AES-CTR mode
    uint64_t nonce;
    
    // Join attribute  
    int32_t join_attr;
    
    // Persistent metadata
    int32_t original_index;
    int32_t local_mult;
    int32_t final_mult;
    int32_t foreign_sum;
    
    // Temporary metadata
    int32_t local_cumsum;
    int32_t local_interval;
    int32_t foreign_cumsum;
    int32_t foreign_interval;
    int32_t local_weight;
    
    // Expansion metadata
    int32_t copy_index;
    int32_t alignment_key;
    
    // Distribution fields
    int32_t dst_idx;
    int32_t index;
    
    // Data attributes (all integers - using vector for flexibility)
    std::vector<int32_t> attributes;
    std::vector<std::string> column_names;
    
    // Constructors
    Entry();
    Entry(const entry_t& c_entry);
    
    // Conversion methods
    entry_t to_entry_t() const;
    void from_entry_t(const entry_t& c_entry);
    
    // Utility methods
    void clear();
    // NOTE: Use CryptoUtils::encrypt_entry() and CryptoUtils::decrypt_entry() for actual encryption/decryption
    
    // Attribute access by column name
    int32_t get_attribute(const std::string& column_name) const;
    bool has_attribute(const std::string& column_name) const;
    bool has_column(const std::string& column_name) const; // Alias for has_attribute
    void set_attribute(const std::string& column_name, int32_t value);
    void add_attribute(const std::string& column_name, int32_t value);
    std::map<std::string, int32_t> get_attributes_map() const;
    
    // Comparison operators for sorting
    bool operator<(const Entry& other) const;
    bool operator==(const Entry& other) const;
    
    // Debug output
    std::string to_string() const;
};

#endif // APP_ENTRY_H