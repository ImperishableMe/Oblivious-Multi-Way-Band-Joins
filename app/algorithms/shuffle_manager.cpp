#include "shuffle_manager.h"
#include "Enclave_u.h"
#include "debug_util.h"
#include "../batch/ecall_batch_collector.h"
#include <algorithm>

// Static member initialization
ShuffleManager* ShuffleManager::current_instance = nullptr;

// Global ocall handlers (extern "C" for EDL)
extern "C" {
    void ocall_append_to_group(int group_idx, entry_t* entry) {
        ShuffleManager::handle_append_to_group(group_idx, entry);
    }
    
    void ocall_get_from_group(int group_idx, entry_t* entry, size_t position) {
        ShuffleManager::handle_get_from_group(group_idx, entry, position);
    }
    
    void ocall_output_element(entry_t* entry, size_t position) {
        ShuffleManager::handle_output_element(entry, position);
    }
}

ShuffleManager::ShuffleManager(sgx_enclave_id_t enclave_id) 
    : eid(enclave_id) {
    DEBUG_INFO("ShuffleManager created");
}

ShuffleManager::~ShuffleManager() {
    clear_current();
}

void ShuffleManager::shuffle(Table& table) {
    if (table.size() <= 1) return;
    
    DEBUG_INFO("ShuffleManager::shuffle starting with %zu entries", table.size());
    
    // Convert table to vector of entries
    std::vector<Entry> entries;
    entries.reserve(table.size());
    for (size_t i = 0; i < table.size(); i++) {
        entries.push_back(table[i]);
    }
    
    // Perform recursive shuffle
    recursive_shuffle(entries);
    
    // Copy back to table
    table.clear();
    for (const auto& entry : entries) {
        table.add_entry(entry);
    }
    
    DEBUG_INFO("ShuffleManager::shuffle complete");
}

void ShuffleManager::recursive_shuffle(std::vector<Entry>& entries) {
    size_t n = entries.size();
    if (n <= 1) return;
    
    DEBUG_TRACE("Recursive shuffle: n=%zu", n);
    
    if (n <= MAX_BATCH_SIZE) {
        shuffle_small(entries);
    } else {
        shuffle_large(entries);
    }
}

void ShuffleManager::shuffle_small(std::vector<Entry>& entries) {
    size_t original_size = entries.size();
    
    DEBUG_INFO("Small shuffle: n=%zu", original_size);
    
    // Pad to power of 2
    size_t padded_size = next_power_of_two(original_size);
    
    if (padded_size > original_size) {
        DEBUG_TRACE("Padding from %zu to %zu (power of 2)", original_size, padded_size);
        pad_entries(entries, padded_size);
    }
    
    // Convert to entry_t array
    std::vector<entry_t> c_entries;
    c_entries.reserve(padded_size);
    for (const auto& e : entries) {
        c_entries.push_back(e.to_entry_t());
    }
    
    // Call 2-way Waksman shuffle
    sgx_status_t status = SGX_SUCCESS;
    sgx_status_t ecall_status = ecall_oblivious_2way_waksman(
        eid, &status, c_entries.data(), padded_size);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("Waksman shuffle failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        return;
    }
    
    // Convert back and truncate to original size
    entries.clear();
    for (size_t i = 0; i < original_size; i++) {
        Entry e;
        e.from_entry_t(c_entries[i]);
        entries.push_back(e);
    }
    
    DEBUG_INFO("Small shuffle complete: %zu entries", original_size);
}

void ShuffleManager::shuffle_large(std::vector<Entry>& entries) {
    size_t original_size = entries.size();
    const size_t k = MERGE_SORT_K;  // Use same K as merge sort (8)
    
    // Pad to multiple of k
    size_t padded_size = next_multiple_of_k(original_size, k);
    if (padded_size > original_size) {
        DEBUG_TRACE("Padding from %zu to %zu (multiple of %zu)", 
                    original_size, padded_size, k);
        pad_entries(entries, padded_size);
    }
    
    DEBUG_INFO("Large shuffle: n=%zu, padded=%zu, k=%zu", 
               original_size, padded_size, k);
    
    // Phase 1: K-way decomposition
    set_as_current();
    
    // Initialize groups
    groups.clear();
    groups.resize(k);
    for (auto& group : groups) {
        group.reserve(padded_size / k);
    }
    
    // Convert to entry_t and call decompose
    std::vector<entry_t> c_entries;
    c_entries.reserve(padded_size);
    for (const auto& e : entries) {
        c_entries.push_back(e.to_entry_t());
    }
    
    sgx_status_t status = SGX_SUCCESS;
    sgx_status_t ecall_status = ecall_k_way_shuffle_decompose(
        eid, &status, c_entries.data(), padded_size);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("K-way decompose failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        clear_current();
        return;
    }
    
    DEBUG_TRACE("Decomposition complete, group sizes:");
    for (size_t i = 0; i < k; i++) {
        DEBUG_TRACE("  Group %zu: %zu entries", i, groups[i].size());
    }
    
    // Phase 2: Recursively shuffle each group
    for (size_t i = 0; i < k; i++) {
        DEBUG_TRACE("Recursively shuffling group %zu", i);
        recursive_shuffle(groups[i]);
    }
    
    // Phase 3: K-way reconstruction
    output_entries.clear();
    output_entries.reserve(padded_size);
    
    // Reset positions for reading
    group_positions.clear();
    group_positions.resize(k, 0);
    
    ecall_status = ecall_k_way_shuffle_reconstruct(
        eid, &status, padded_size);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("K-way reconstruct failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        clear_current();
        return;
    }
    
    // Move output to entries vector
    entries = std::move(output_entries);
    
    clear_current();
    
    // Truncate to original size
    if (entries.size() > original_size) {
        entries.resize(original_size);
    }
    
    DEBUG_INFO("Large shuffle complete: %zu entries", original_size);
}

void ShuffleManager::pad_entries(std::vector<Entry>& entries, size_t target_size) {
    size_t current_size = entries.size();
    if (current_size >= target_size) {
        return;  // No padding needed
    }
    
    // Get encryption status from existing entries
    uint8_t encryption_status = 0;
    if (!entries.empty()) {
        encryption_status = entries[0].is_encrypted;
    }
    
    size_t padding_needed = target_size - current_size;
    DEBUG_TRACE("Adding %zu padding entries", padding_needed);
    
    // Reserve space for efficiency
    entries.reserve(target_size);
    
    // Create batch collector for SORT_PADDING transformation
    EcallBatchCollector collector(eid, OP_ECALL_TRANSFORM_SET_SORT_PADDING);
    
    // Add padding entries
    for (size_t i = 0; i < padding_needed; i++) {
        Entry padding;
        padding.is_encrypted = encryption_status;
        entries.push_back(padding);
        
        // Add to batch for transformation to SORT_PADDING
        collector.add_operation(entries.back());
    }
    
    // Flush batch to set field_type = SORT_PADDING and other fields
    collector.flush();
}

// Ocall handlers
void ShuffleManager::handle_append_to_group(int group_idx, entry_t* entry) {
    if (!current_instance || group_idx < 0 || group_idx >= MERGE_SORT_K) {
        DEBUG_ERROR("Invalid append_to_group: group_idx=%d, current=%p", 
                    group_idx, current_instance);
        return;
    }
    
    Entry e;
    e.from_entry_t(*entry);
    current_instance->groups[group_idx].push_back(e);
}

void ShuffleManager::handle_get_from_group(int group_idx, entry_t* entry, size_t position) {
    if (!current_instance || group_idx < 0 || group_idx >= MERGE_SORT_K) {
        DEBUG_ERROR("Invalid get_from_group: group_idx=%d, position=%zu", 
                    group_idx, position);
        return;
    }
    
    if (position < current_instance->groups[group_idx].size()) {
        *entry = current_instance->groups[group_idx][position].to_entry_t();
    } else {
        DEBUG_ERROR("Position %zu out of bounds for group %d (size=%zu)", 
                    position, group_idx, current_instance->groups[group_idx].size());
    }
}

void ShuffleManager::handle_output_element(entry_t* entry, size_t position) {
    if (!current_instance) {
        DEBUG_ERROR("No current instance for output_element");
        return;
    }
    
    (void)position;  // Position not needed for sequential output
    Entry e;
    e.from_entry_t(*entry);
    current_instance->output_entries.push_back(e);
}

void ShuffleManager::set_as_current() {
    current_instance = this;
}

void ShuffleManager::clear_current() {
    if (current_instance == this) {
        current_instance = nullptr;
    }
}