#include "types.h"
#include <cstring>
#include <algorithm>
#include "../common/debug_util.h"

//////////////////////////////////////////////////////////////////////////////
// Entry Implementation
//////////////////////////////////////////////////////////////////////////////

Entry::Entry() 
    : field_type(SOURCE), 
      equality_type(EQ), 
      is_encrypted(false),
      join_attr(0.0), 
      original_index(0), 
      local_mult(0), 
      final_mult(0),
      foreign_sum(0), 
      local_cumsum(0), 
      local_interval(0),
      foreign_cumsum(0), 
      foreign_interval(0), 
      local_weight(0),
      copy_index(0), 
      alignment_key(0) {
}

Entry::Entry(const entry_t& c_entry) {
    from_entry_t(c_entry);
}

entry_t Entry::to_entry_t() const {
    DEBUG_TRACE("Entry::to_entry_t() - Converting Entry to entry_t");
    DEBUG_TRACE("  field_type=%d, is_encrypted=%d, join_attr=%d", 
                field_type, is_encrypted, join_attr);
    
    entry_t result;
    memset(&result, 0, sizeof(entry_t));
    
    // Copy basic fields
    result.field_type = field_type;
    result.equality_type = equality_type;
    result.is_encrypted = is_encrypted;
    result.nonce = nonce;  // Don't forget the nonce!
    result.join_attr = join_attr;
    result.original_index = original_index;
    result.local_mult = local_mult;
    result.final_mult = final_mult;
    result.foreign_sum = foreign_sum;
    result.local_cumsum = local_cumsum;
    result.local_interval = local_interval;
    result.foreign_cumsum = foreign_cumsum;
    result.foreign_interval = foreign_interval;
    result.local_weight = local_weight;
    result.copy_index = copy_index;
    result.alignment_key = alignment_key;
    
    // Copy attributes (up to MAX_ATTRIBUTES)
    size_t attr_count = std::min(attributes.size(), (size_t)MAX_ATTRIBUTES);
    for (size_t i = 0; i < attr_count; i++) {
        result.attributes[i] = attributes[i];
    }
    
    // Copy column names (up to MAX_ATTRIBUTES)
    size_t col_count = std::min(column_names.size(), (size_t)MAX_ATTRIBUTES);
    for (size_t i = 0; i < col_count; i++) {
        strncpy(result.column_names[i], column_names[i].c_str(), MAX_COLUMN_NAME_LEN - 1);
        result.column_names[i][MAX_COLUMN_NAME_LEN - 1] = '\0';
    }
    
    return result;
}

void Entry::from_entry_t(const entry_t& c_entry) {
    DEBUG_TRACE("Entry::from_entry_t() - Converting entry_t to Entry");
    DEBUG_TRACE("  field_type=%d, is_encrypted=%d, join_attr=%d", 
                c_entry.field_type, c_entry.is_encrypted, c_entry.join_attr);
    
    field_type = c_entry.field_type;
    equality_type = c_entry.equality_type;
    is_encrypted = c_entry.is_encrypted;
    nonce = c_entry.nonce;  // Copy nonce too!
    join_attr = c_entry.join_attr;
    original_index = c_entry.original_index;
    local_mult = c_entry.local_mult;
    final_mult = c_entry.final_mult;
    foreign_sum = c_entry.foreign_sum;
    local_cumsum = c_entry.local_cumsum;
    local_interval = c_entry.local_interval;
    foreign_cumsum = c_entry.foreign_cumsum;
    foreign_interval = c_entry.foreign_interval;
    local_weight = c_entry.local_weight;
    copy_index = c_entry.copy_index;
    alignment_key = c_entry.alignment_key;
    
    // Clear and copy column names first to determine actual column count
    column_names.clear();
    int actual_columns = 0;
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (strlen(c_entry.column_names[i]) == 0) {
            // Stop at first empty column name
            break;
        }
        column_names.push_back(std::string(c_entry.column_names[i]));
        actual_columns++;
    }
    
    // Clear and copy only the actual number of attributes
    attributes.clear();
    for (int i = 0; i < actual_columns && i < MAX_ATTRIBUTES; i++) {
        attributes.push_back(c_entry.attributes[i]);
    }
}

void Entry::clear() {
    *this = Entry();  // Reset to default values
}

void Entry::encrypt() {
    // Placeholder - actual encryption happens in SGX
    is_encrypted = true;
}

void Entry::decrypt() {
    // Placeholder - actual decryption happens in SGX
    is_encrypted = false;
}

int32_t Entry::get_attribute(const std::string& column_name) const {
    // Find the column index
    for (size_t i = 0; i < column_names.size(); i++) {
        if (column_names[i] == column_name) {
            if (i < attributes.size()) {
                return attributes[i];
            }
            break;
        }
    }
    return 0;  // Return 0 if column not found
}

void Entry::set_attribute(const std::string& column_name, int32_t value) {
    // Find the column index
    for (size_t i = 0; i < column_names.size(); i++) {
        if (column_names[i] == column_name) {
            if (i < attributes.size()) {
                attributes[i] = value;
            }
            return;
        }
    }
    // If column not found, add it
    add_attribute(column_name, value);
}

bool Entry::has_column(const std::string& column_name) const {
    for (const auto& name : column_names) {
        if (name == column_name) {
            return true;
        }
    }
    return false;
}

void Entry::add_attribute(const std::string& column_name, int32_t value) {
    column_names.push_back(column_name);
    attributes.push_back(value);
}

std::map<std::string, int32_t> Entry::get_attributes_map() const {
    std::map<std::string, int32_t> result;
    for (size_t i = 0; i < column_names.size() && i < attributes.size(); i++) {
        result[column_names[i]] = attributes[i];
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////////
// Table Implementation
//////////////////////////////////////////////////////////////////////////////

Table::Table() : num_columns(0) {
}

Table::Table(const std::string& name) : table_name(name), num_columns(0) {
}

void Table::add_entry(const Entry& entry) {
    entries.push_back(entry);
}

Entry& Table::get_entry(size_t index) {
    return entries[index];
}

const Entry& Table::get_entry(size_t index) const {
    return entries[index];
}

void Table::set_entry(size_t index, const Entry& entry) {
    if (index < entries.size()) {
        entries[index] = entry;
    }
}

Entry& Table::operator[](size_t index) {
    return entries[index];
}

const Entry& Table::operator[](size_t index) const {
    return entries[index];
}

size_t Table::size() const {
    return entries.size();
}

void Table::clear() {
    entries.clear();
}

void Table::set_all_field_type(entry_type_t type) {
    for (auto& entry : entries) {
        entry.field_type = type;
    }
}

void Table::initialize_original_indices() {
    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].original_index = i;
    }
}

void Table::initialize_leaf_multiplicities() {
    for (auto& entry : entries) {
        entry.local_mult = 1;
        entry.final_mult = 1;
    }
}

std::vector<entry_t> Table::to_entry_t_vector() const {
    std::vector<entry_t> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.push_back(entry.to_entry_t());
    }
    return result;
}

void Table::from_entry_t_vector(const std::vector<entry_t>& c_entries) {
    entries.clear();
    entries.reserve(c_entries.size());
    for (const auto& c_entry : c_entries) {
        entries.emplace_back(c_entry);
    }
}

void Table::set_table_name(const std::string& name) {
    table_name = name;
}

std::string Table::get_table_name() const {
    return table_name;
}

void Table::set_num_columns(size_t n) {
    num_columns = n;
}

size_t Table::get_num_columns() const {
    return num_columns;
}

std::vector<Entry>::iterator Table::begin() {
    return entries.begin();
}

std::vector<Entry>::iterator Table::end() {
    return entries.end();
}

std::vector<Entry>::const_iterator Table::begin() const {
    return entries.begin();
}

std::vector<Entry>::const_iterator Table::end() const {
    return entries.end();
}

Table::EncryptionStatus Table::get_encryption_status() const {
    if (entries.empty()) {
        return UNENCRYPTED;  // Empty table is considered unencrypted
    }
    
    bool first_is_encrypted = entries[0].is_encrypted;
    
    for (size_t i = 1; i < entries.size(); ++i) {
        if (entries[i].is_encrypted != first_is_encrypted) {
            return MIXED;  // Found a mismatch
        }
    }
    
    return first_is_encrypted ? ENCRYPTED : UNENCRYPTED;
}

//////////////////////////////////////////////////////////////////////////////
// JoinCondition Implementation
//////////////////////////////////////////////////////////////////////////////

JoinCondition::JoinCondition() {
}

JoinCondition::JoinCondition(const std::string& parent_tbl, const std::string& child_tbl,
                           const std::string& parent_col, const std::string& child_col,
                           const Bound& lower, const Bound& upper)
    : parent_table(parent_tbl), child_table(child_tbl),
      parent_column(parent_col), child_column(child_col),
      lower_bound(lower), upper_bound(upper) {
}

JoinCondition JoinCondition::equality(const std::string& parent_tbl, const std::string& child_tbl,
                                     const std::string& parent_col, const std::string& child_col) {
    return JoinCondition(parent_tbl, child_tbl, parent_col, child_col,
                        Bound(0.0, EQ), Bound(0.0, EQ));
}

JoinCondition JoinCondition::band(const std::string& parent_tbl, const std::string& child_tbl,
                                 const std::string& parent_col, const std::string& child_col,
                                 double lower_offset, double upper_offset,
                                 bool lower_inclusive, bool upper_inclusive) {
    equality_type_t lower_eq = lower_inclusive ? EQ : NEQ;
    equality_type_t upper_eq = upper_inclusive ? EQ : NEQ;
    return JoinCondition(parent_tbl, child_tbl, parent_col, child_col,
                        Bound(lower_offset, lower_eq), Bound(upper_offset, upper_eq));
}

std::pair<Entry, Entry> JoinCondition::create_boundary_entries(const Entry& target_entry) const {
    Entry start_entry = target_entry;
    Entry end_entry = target_entry;
    
    // Set field types
    start_entry.field_type = START;
    end_entry.field_type = END;
    
    // Apply bounds
    start_entry.join_attr += lower_bound.deviation;
    start_entry.equality_type = lower_bound.equality;
    
    end_entry.join_attr += upper_bound.deviation;
    end_entry.equality_type = upper_bound.equality;
    
    return std::make_pair(start_entry, end_entry);
}

// ============================================================================
// Oblivious Operations Implementation
// ============================================================================

#include <stdexcept>
#include <climits>
#include "../common/debug_util.h"
#include "Enclave_u.h"

void Table::check_sgx_status(sgx_status_t status, const std::string& operation) {
    if (status != SGX_SUCCESS) {
        throw std::runtime_error("SGX error in " + operation + ": " + std::to_string(status));
    }
}

Table Table::map(sgx_enclave_id_t eid,
                std::function<sgx_status_t(sgx_enclave_id_t, entry_t*)> transform_func) const {
    DEBUG_DEBUG("Map: Processing %zu entries", size());
    Table output = *this;  // Copy structure and data
    
    // Apply transform to each entry independently
    for (size_t i = 0; i < output.size(); i++) {
        DEBUG_DEBUG("Map: Processing entry %zu/%zu", i, output.size());
        entry_t entry = output.entries[i].to_entry_t();
        
        // Apply the transform ecall
        sgx_status_t status = transform_func(eid, &entry);
        check_sgx_status(status, "Map transform");
        
        // Store back the transformed entry
        output.entries[i].from_entry_t(entry);
    }
    
    return output;
}

void Table::linear_pass(sgx_enclave_id_t eid,
                       std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> window_func) {
    // Process sliding window of size 2
    for (size_t i = 0; i < size() - 1; i++) {
        entry_t e1 = entries[i].to_entry_t();
        entry_t e2 = entries[i + 1].to_entry_t();
        
        // Apply window function
        sgx_status_t status = window_func(eid, &e1, &e2);
        check_sgx_status(status, "LinearPass window");
        
        // Store back modified entries
        entries[i].from_entry_t(e1);
        entries[i + 1].from_entry_t(e2);
    }
}

void Table::parallel_pass(Table& other, sgx_enclave_id_t eid,
                         std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> pair_func) {
    if (size() != other.size()) {
        throw std::runtime_error("ParallelPass: Tables must have same size");
    }
    
    // Process aligned pairs
    for (size_t i = 0; i < size(); i++) {
        entry_t e1 = entries[i].to_entry_t();
        entry_t e2 = other.entries[i].to_entry_t();
        
        // Apply pair function
        sgx_status_t status = pair_func(eid, &e1, &e2);
        check_sgx_status(status, "ParallelPass pair");
        
        // Store back modified entries
        entries[i].from_entry_t(e1);
        other.entries[i].from_entry_t(e2);
    }
}

void Table::compare_and_swap(size_t i, size_t j, sgx_enclave_id_t eid,
                            std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> compare_swap_func) {
    // Convert to entry_t
    entry_t e1 = entries[i].to_entry_t();
    entry_t e2 = entries[j].to_entry_t();
    
    // Call the comparator which performs oblivious swap if needed
    sgx_status_t status = compare_swap_func(eid, &e1, &e2);
    check_sgx_status(status, "CompareAndSwap");
    
    // Convert back to Entry
    entries[i].from_entry_t(e1);
    entries[j].from_entry_t(e2);
}

bool Table::is_power_of_two(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

size_t Table::next_power_of_two(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    if (sizeof(size_t) > 4) n |= n >> 32;
    return n + 1;
}

void Table::oblivious_sort(sgx_enclave_id_t eid,
                          std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> compare_swap_func) {
    size_t n = size();
    DEBUG_INFO("ObliviousSort: Starting bitonic sort of %zu entries", n);
    
    // Check for empty or single element
    if (n <= 1) {
        DEBUG_INFO("ObliviousSort: Table has %zu entries, nothing to sort", n);
        return;
    }
    
    // For bitonic sort to work correctly, we need power of 2 size
    // Pad with EMPTY entries if needed
    size_t padded_size = next_power_of_two(n);
    size_t padding_needed = 0;
    if (padded_size != n) {
        DEBUG_INFO("ObliviousSort: Padding from %zu to %zu entries", n, padded_size);
        padding_needed = padded_size - n;
        
        // Add padding EMPTY entries using ecall
        for (size_t i = 0; i < padding_needed; i++) {
            Entry dummy;
            entry_t dummy_entry = dummy.to_entry_t();
            ecall_transform_set_empty(eid, &dummy_entry);
            dummy.from_entry_t(dummy_entry);
            add_entry(dummy);
        }
    }
    
    // Bitonic sort implementation
    // This creates a bitonic sequence then sorts it
    for (size_t k = 2; k <= padded_size; k *= 2) {
        // Build bitonic sequences of size k
        for (size_t j = k/2; j > 0; j /= 2) {
            // Compare and swap with distance j
            for (size_t i = 0; i < padded_size; i++) {
                size_t ixj = i ^ j;  // XOR gives us the paired index
                
                // Only process each pair once (when i < ixj)
                if (ixj > i) {
                    // Determine sort direction based on bitonic pattern
                    // (i & k) == 0 means we're in an ascending part
                    if ((i & k) == 0) {
                        // Ascending: normal compare and swap
                        compare_and_swap(i, ixj, eid, compare_swap_func);
                    } else {
                        // Descending: reverse compare and swap
                        // We swap the positions to reverse the comparison
                        compare_and_swap(ixj, i, eid, compare_swap_func);
                    }
                }
            }
        }
    }
    
    // Remove padding entries if we added any
    if (padding_needed > 0) {
        DEBUG_INFO("ObliviousSort: Removing %zu padding entries", padding_needed);
        
        // Remove EMPTY entries from the end
        // They should be sorted to the end due to EMPTY having max values
        for (size_t i = 0; i < padding_needed; i++) {
            entries.pop_back();
        }
    }
    
    DEBUG_INFO("ObliviousSort: Bitonic sort completed");
}

Table Table::oblivious_expand(sgx_enclave_id_t eid) const {
    // Calculate total size after expansion
    size_t total_size = 0;
    std::vector<uint32_t> multiplicities;
    
    // We need to get final_mult from each entry
    // For now, we'll use a special ecall to read it
    // TODO: Add ecall_get_final_mult to read multiplicity securely
    
    for (size_t i = 0; i < size(); i++) {
        // For testing, assume final_mult is accessible
        // In production, this would use an ecall
        uint32_t mult = entries[i].final_mult;
        multiplicities.push_back(mult);
        total_size += mult;
    }
    
    // Create expanded table
    Table expanded;
    expanded.set_table_name(table_name + "_expanded");
    
    // Expand each entry according to its multiplicity
    for (size_t i = 0; i < size(); i++) {
        uint32_t mult = multiplicities[i];
        
        for (uint32_t j = 0; j < mult; j++) {
            Entry copy = entries[i];
            copy.copy_index = j;  // Track which copy this is
            expanded.add_entry(copy);
        }
    }
    
    return expanded;
}

Table Table::horizontal_concatenate(const Table& left, const Table& right) {
    if (left.size() != right.size()) {
        throw std::runtime_error("HorizontalConcatenate: Tables must have same number of rows");
    }
    
    Table result;
    result.set_table_name(left.get_table_name() + "_" + right.get_table_name());
    
    // Concatenate each row
    for (size_t i = 0; i < left.size(); i++) {
        Entry combined = left.entries[i];
        
        // Add all attributes from right table
        auto right_attrs = right.entries[i].get_attributes_map();
        for (const auto& [col_name, value] : right_attrs) {
            combined.add_attribute(col_name, value);
        }
        
        result.add_entry(combined);
    }
    
    return result;
}