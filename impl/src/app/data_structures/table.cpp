#include "table.h"
#include <stdexcept>
#include <climits>
#include "../../common/debug_util.h"
#include "../Enclave_u.h"
#include "../batch/ecall_batch_collector.h"

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
        entries[i].original_index = static_cast<int32_t>(i);
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

// Schema management methods for slim mode migration
void Table::set_schema(const std::vector<std::string>& columns) {
    schema_column_names = columns;
    // Also update num_columns to match
    if (num_columns == 0 || num_columns != columns.size()) {
        num_columns = columns.size();
    }
}

std::vector<std::string> Table::get_schema() const {
    return schema_column_names;
}

size_t Table::get_column_index(const std::string& col_name) const {
    for (size_t i = 0; i < schema_column_names.size(); i++) {
        if (schema_column_names[i] == col_name) {
            return i;
        }
    }
    // Column not found in schema
    throw std::runtime_error("Column not found: " + col_name);
}

bool Table::has_column(const std::string& col_name) const {
    // Check schema only
    for (const auto& name : schema_column_names) {
        if (name == col_name) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Table::generate_generic_schema(size_t num_columns) {
    std::vector<std::string> schema;
    for (size_t i = 0; i < num_columns; i++) {
        schema.push_back("col" + std::to_string(i + 1));
    }
    return schema;
}

int32_t Table::get_attribute(size_t row, const std::string& col_name) const {
    if (row >= entries.size()) {
        throw std::out_of_range("Row index out of bounds");
    }
    size_t col_index = get_column_index(col_name);
    const Entry& entry = entries[row];
    if (col_index >= entry.attributes.size()) {
        throw std::out_of_range("Column index out of bounds");
    }
    return entry.attributes[col_index];
}

void Table::set_attribute(size_t row, const std::string& col_name, int32_t value) {
    if (row >= entries.size()) {
        throw std::out_of_range("Row index out of bounds");
    }
    size_t col_index = get_column_index(col_name);
    Entry& entry = entries[row];
    if (col_index >= entry.attributes.size()) {
        // Expand attributes if needed
        entry.attributes.resize(col_index + 1, NULL_VALUE);
    }
    entry.attributes[col_index] = value;
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

// ============================================================================
// Oblivious Operations Implementation
// ============================================================================

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
        // Removed TRACE log to reduce debug output volume
        
        // Log the Entry before conversion
        #if DEBUG_LEVEL >= 3
        const Entry& orig_entry = output.entries[i];
        DEBUG_DEBUG("  Before: field_type=%d, is_encrypted=%d, join_attr=%d, original_index=%u",
                    orig_entry.field_type, orig_entry.is_encrypted, 
                    orig_entry.join_attr, orig_entry.original_index);
        #else
        (void)0; // Avoid empty statement warning
        #endif
        
        entry_t entry = output.entries[i].to_entry_t();
        
        // Log the entry_t before ecall
        DEBUG_DEBUG("  entry_t before ecall: field_type=%d, is_encrypted=%d, join_attr=%d",
                    entry.field_type, entry.is_encrypted, entry.join_attr);
        
        // Apply the transform ecall
        sgx_status_t status = transform_func(eid, &entry);
        check_sgx_status(status, "Map transform");
        
        // Log the entry_t after ecall
        DEBUG_DEBUG("  entry_t after ecall: field_type=%d, is_encrypted=%d, join_attr=%d",
                    entry.field_type, entry.is_encrypted, entry.join_attr);
        
        // Store back the transformed entry
        output.entries[i].from_entry_t(entry);
        
        // Log the Entry after conversion
        DEBUG_DEBUG("  After: field_type=%d, is_encrypted=%d, join_attr=%d, original_index=%u",
                    output.entries[i].field_type, output.entries[i].is_encrypted,
                    output.entries[i].join_attr, output.entries[i].original_index);
    }
    DEBUG_DEBUG("Map: Complete");
    
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
    // Pad with SORT_PADDING entries if needed
    size_t padded_size = next_power_of_two(n);
    size_t padding_needed = 0;
    if (padded_size != n) {
        DEBUG_INFO("ObliviousSort: Padding from %zu to %zu entries", n, padded_size);
        padding_needed = padded_size - n;
        
        // Add padding SORT_PADDING entries using ecall
        for (size_t i = 0; i < padding_needed; i++) {
            Entry dummy;
            entry_t dummy_entry = dummy.to_entry_t();
            ecall_transform_set_sort_padding(eid, &dummy_entry);
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
        
        // Remove SORT_PADDING entries from the end
        // They should be sorted to the end due to SORT_PADDING having max values
        for (size_t i = 0; i < padding_needed; i++) {
            entries.pop_back();
        }
    }
    
    DEBUG_INFO("ObliviousSort: Bitonic sort completed");
}

void Table::distribute_pass(sgx_enclave_id_t eid, size_t distance,
                           std::function<void(sgx_enclave_id_t, entry_t*, entry_t*, size_t)> func) {
    // Apply function to pairs of entries at given distance
    // Reversed loop: process from right to left to avoid interference
    // This ensures entries moving right don't interfere with unprocessed entries
    for (size_t i = entries.size() - distance; i > 0; i--) {
        entry_t e1 = entries[i - 1].to_entry_t();
        entry_t e2 = entries[i - 1 + distance].to_entry_t();
        
        // Apply the function
        func(eid, &e1, &e2, distance);
        
        // Update entries from modified entry_t structures
        entries[i - 1] = Entry(e1);
        entries[i - 1 + distance] = Entry(e2);
    }
    
    // Handle i = 0 case separately to avoid underflow
    if (distance < entries.size()) {
        entry_t e1 = entries[0].to_entry_t();
        entry_t e2 = entries[distance].to_entry_t();
        
        func(eid, &e1, &e2, distance);
        
        entries[0] = Entry(e1);
        entries[distance] = Entry(e2);
    }
}

void Table::batched_distribute_pass(sgx_enclave_id_t eid, size_t distance, OpEcall op_type, int32_t* params) {
    DEBUG_TRACE("Table::batched_distribute_pass: Starting with distance %zu, op_type=%d", distance, op_type);
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all pairs at given distance
    // Process from right to left (same as non-batched version)
    for (size_t i = entries.size() - distance; i > 0; i--) {
        collector.add_operation(entries[i - 1], entries[i - 1 + distance], params);
    }
    
    // Handle i = 0 case separately to avoid underflow
    if (distance < entries.size()) {
        collector.add_operation(entries[0], entries[distance], params);
    }
    
    // Flush the batch - this writes back to entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_distribute_pass: Complete");
}

Table Table::oblivious_expand(sgx_enclave_id_t /*eid*/) const {
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

// ============================================================================
// Batched Operations Implementation
// ============================================================================

Table Table::batched_map(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params) const {
    DEBUG_TRACE("Table::batched_map: Starting with %zu entries, op_type=%d", entries.size(), op_type);
    
    Table result(table_name + "_mapped");
    result.set_num_columns(num_columns);
    
    // Copy entries to result first (since we can't modify const entries)
    for (const auto& entry : entries) {
        result.add_entry(entry);
    }
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all operations for map (single entry operations)
    for (size_t i = 0; i < result.entries.size(); i++) {
        collector.add_operation(result.entries[i], params);
    }
    
    // Flush the batch - this writes back to result.entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_map: Complete with %zu entries", result.size());
    return result;
}

void Table::batched_linear_pass(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params) {
    if (entries.size() < 2) return;
    
    DEBUG_TRACE("Table::batched_linear_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all window operations - work directly with Entry objects
    for (size_t i = 0; i < entries.size() - 1; i++) {
        collector.add_operation(entries[i], entries[i+1], params);
    }
    
    // Flush the batch - this writes back to entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_linear_pass: Complete");
}

void Table::batched_parallel_pass(Table& other, sgx_enclave_id_t eid, OpEcall op_type, int32_t* params) {
    if (entries.size() != other.entries.size()) {
        throw std::runtime_error("Tables must have the same size for parallel pass");
    }
    
    DEBUG_TRACE("Table::batched_parallel_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all parallel operations - work directly with Entry objects
    for (size_t i = 0; i < entries.size(); i++) {
        collector.add_operation(entries[i], other.entries[i], params);
    }
    
    // Flush the batch - this writes back to both tables' entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_parallel_pass: Complete");
}

void Table::batched_oblivious_sort(sgx_enclave_id_t eid, OpEcall op_type) {
    if (entries.size() <= 1) return;
    
    DEBUG_TRACE("Table::batched_oblivious_sort: Starting with %zu entries, op_type=%d", entries.size(), op_type);
    
    // Pad to power of 2 for bitonic sort
    size_t original_size = entries.size();
    size_t padded_size = next_power_of_two(original_size);
    
    // Add padding entries if needed
    while (entries.size() < padded_size) {
        Entry padding;
        padding.field_type = SORT_PADDING;
        add_entry(padding);
    }
    
    // Bitonic sort implementation with batching - work directly with Entry objects
    for (size_t k = 2; k <= padded_size; k *= 2) {
        for (size_t j = k / 2; j > 0; j /= 2) {
            // Create batch collector for this pass
            EcallBatchCollector collector(eid, op_type);
            
            // Add all comparisons for this pass
            for (size_t i = 0; i < padded_size; i++) {
                size_t ij = i ^ j;
                if (ij > i) {
                    if ((i & k) == 0) {
                        // Ascending
                        collector.add_operation(entries[i], entries[ij], nullptr);
                    } else {
                        // Descending
                        collector.add_operation(entries[ij], entries[i], nullptr);
                    }
                }
            }
            
            // Flush this pass - writes back to original Entry objects
            collector.flush();
        }
    }
    
    // Remove padding entries
    entries.resize(original_size);
    
    DEBUG_TRACE("Table::batched_oblivious_sort: Complete");
}

void Table::add_batched_padding(size_t count, sgx_enclave_id_t eid, uint8_t encryption_status) {
    if (count == 0) {
        return;
    }
    
    DEBUG_TRACE("Table::add_batched_padding: Adding %zu padding entries", count);
    
    // Reserve space for new entries
    entries.reserve(entries.size() + count);
    
    // Create batch collector for padding creation
    EcallBatchCollector collector(eid, OP_ECALL_TRANSFORM_CREATE_DIST_PADDING);
    
    // Create padding entries in batches
    for (size_t i = 0; i < count; i++) {
        Entry padding;
        // Initialize entry_t structure instead of Entry class
        entry_t padding_entry;
        memset(&padding_entry, 0, sizeof(entry_t));
        padding_entry.is_encrypted = encryption_status;  // Match table's encryption
        padding = Entry(padding_entry);
        
        // Add to table first
        entries.push_back(padding);
        
        // Add to batch for transformation
        collector.add_operation(entries.back());
    }
    
    // Flush any remaining operations
    collector.flush();
    
    DEBUG_TRACE("Table::add_batched_padding: Complete - added %zu entries", count);
}

