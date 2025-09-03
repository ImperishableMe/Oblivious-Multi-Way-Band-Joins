#include "shuffle_manager.h"
#include "Enclave_u.h"
#include "debug_util.h"
#include "../crypto/crypto_utils.h"
#include <algorithm>

// Static member initialization
ShuffleManager* ShuffleManager::current_instance = nullptr;

// Global ocall handlers (extern "C" for EDL)
extern "C" {
    void ocall_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size) {
        ShuffleManager::handle_flush_to_group(group_idx, buffer, buffer_size);
    }
    
    void ocall_refill_from_group(int group_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled) {
        ShuffleManager::handle_refill_from_group(group_idx, buffer, buffer_size, actual_filled);
    }
    
    void ocall_flush_output(entry_t* buffer, size_t buffer_size) {
        ShuffleManager::handle_flush_output(buffer, buffer_size);
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
    
    // Calculate and apply one-time padding
    size_t original_size = entries.size();
    size_t padded_size = calculate_shuffle_padding(original_size);
    if (padded_size > original_size) {
        DEBUG_INFO("Padding from %zu to %zu for shuffle", original_size, padded_size);
        pad_entries(entries, padded_size);
    }
    
    // Perform recursive shuffle (no more padding inside)
    recursive_shuffle(entries);
    
    // Copy back to table (keep padded size - truncation happens after merge sort)
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
    size_t n = entries.size();
    
    DEBUG_INFO("Small shuffle: n=%zu", n);
    
    // No padding here - already done in shuffle()
    
    // Convert to entry_t array
    std::vector<entry_t> c_entries;
    c_entries.reserve(n);
    for (const auto& e : entries) {
        c_entries.push_back(e.to_entry_t());
    }
    
    // Call 2-way Waksman shuffle
    sgx_status_t status = SGX_SUCCESS;
    sgx_status_t ecall_status = ecall_oblivious_2way_waksman(
        eid, &status, c_entries.data(), n);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("Waksman shuffle failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        return;
    }
    
    // Convert back all entries (including padding)
    entries.clear();
    for (size_t i = 0; i < n; i++) {
        Entry e;
        e.from_entry_t(c_entries[i]);
        entries.push_back(e);
    }
    
    DEBUG_INFO("Small shuffle complete: %zu entries", n);
}

void ShuffleManager::shuffle_large(std::vector<Entry>& entries) {
    size_t n = entries.size();
    const size_t k = MERGE_SORT_K;  // Use same K as merge sort (8)
    
    // No padding here - already done in shuffle()
    // n should already be a valid size (2^a * k^b)
    
    DEBUG_INFO("Large shuffle: n=%zu, k=%zu", n, k);
    
    // Phase 1: K-way decomposition
    set_as_current();
    
    // Initialize groups
    groups.clear();
    groups.resize(k);
    for (auto& group : groups) {
        group.reserve(n / k);
    }
    
    // Convert to entry_t and call decompose
    std::vector<entry_t> c_entries;
    c_entries.reserve(n);
    for (const auto& e : entries) {
        c_entries.push_back(e.to_entry_t());
    }
    
    sgx_status_t status = SGX_SUCCESS;
    sgx_status_t ecall_status = ecall_k_way_shuffle_decompose(
        eid, &status, c_entries.data(), n);
    
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
    output_entries.reserve(n);
    
    // Reset positions for reading
    group_positions.clear();
    group_positions.resize(k, 0);
    
    ecall_status = ecall_k_way_shuffle_reconstruct(
        eid, &status, n);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("K-way reconstruct failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        clear_current();
        return;
    }
    
    // Move output to entries vector (keep all entries including padding)
    entries = std::move(output_entries);
    
    clear_current();
    
    DEBUG_INFO("Large shuffle complete: %zu entries", n);
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
    
    // Add padding entries with SORT_PADDING field_type
    for (size_t i = 0; i < padding_needed; i++) {
        Entry padding;
        padding.field_type = SORT_PADDING;  // Mark as padding
        // Initialize attributes to sentinel values
        for (int j = 0; j < MAX_ATTRIBUTES; j++) {
            padding.attributes[j] = JOIN_ATTR_POS_INF;  // Use positive infinity as sentinel
        }
        padding.join_attr = JOIN_ATTR_POS_INF;
        
        // Encrypt if other entries are encrypted
        if (encryption_status) {
            padding.is_encrypted = 0;  // Start unencrypted
            crypto_status_t enc_status;
            entry_t entry_c = padding.to_entry_t();
            sgx_status_t ecall_ret = ecall_encrypt_entry(eid, &enc_status, &entry_c);
            if (ecall_ret == SGX_SUCCESS && enc_status == CRYPTO_SUCCESS) {
                padding.from_entry_t(entry_c);
            }
        } else {
            padding.is_encrypted = 0;
        }
        
        entries.push_back(padding);
    }
}

// Ocall handlers for buffered I/O
void ShuffleManager::handle_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size) {
    if (!current_instance || group_idx < 0 || group_idx >= MERGE_SORT_K) {
        DEBUG_ERROR("Invalid flush_to_group: group_idx=%d, current=%p", 
                    group_idx, current_instance);
        return;
    }
    
    // Append buffer contents to the specified group
    for (size_t i = 0; i < buffer_size; i++) {
        Entry e;
        e.from_entry_t(buffer[i]);
        current_instance->groups[group_idx].push_back(e);
    }
    
    DEBUG_TRACE("Flushed %zu entries to group %d (total=%zu)", 
                buffer_size, group_idx, current_instance->groups[group_idx].size());
}

void ShuffleManager::handle_refill_from_group(int group_idx, entry_t* buffer, 
                                               size_t buffer_size, size_t* actual_filled) {
    if (!current_instance || group_idx < 0 || group_idx >= MERGE_SORT_K) {
        DEBUG_ERROR("Invalid refill_from_group: group_idx=%d", group_idx);
        *actual_filled = 0;
        return;
    }
    
    size_t& pos = current_instance->group_positions[group_idx];
    const auto& group = current_instance->groups[group_idx];
    size_t available = group.size() - pos;
    size_t to_fill = std::min(buffer_size, available);
    
    // Fill buffer from current position in group
    for (size_t i = 0; i < to_fill; i++) {
        buffer[i] = group[pos + i].to_entry_t();
    }
    
    pos += to_fill;
    *actual_filled = to_fill;
    
    DEBUG_TRACE("Refilled %zu entries from group %d (pos=%zu/%zu)", 
                to_fill, group_idx, pos, group.size());
}

void ShuffleManager::handle_flush_output(entry_t* buffer, size_t buffer_size) {
    if (!current_instance) {
        DEBUG_ERROR("No current instance for flush_output");
        return;
    }
    
    // Append buffer contents to output
    for (size_t i = 0; i < buffer_size; i++) {
        Entry e;
        e.from_entry_t(buffer[i]);
        current_instance->output_entries.push_back(e);
    }
    
    DEBUG_TRACE("Flushed %zu entries to output (total=%zu)", 
                buffer_size, current_instance->output_entries.size());
}

void ShuffleManager::set_as_current() {
    current_instance = this;
}

void ShuffleManager::clear_current() {
    if (current_instance == this) {
        current_instance = nullptr;
    }
}

// Calculate padding target: smallest m >= n where m = 2^a * k^b
size_t ShuffleManager::calculate_shuffle_padding(size_t n) {
    if (n <= MAX_BATCH_SIZE) {
        // Small vector: just pad to power of 2
        return next_power_of_two(n);
    }
    
    // Large vector: need m = 2^a * k^b
    const size_t k = MERGE_SORT_K;
    
    // First determine b: number of k-way decomposition levels needed
    // After b levels, we want size <= MAX_BATCH_SIZE
    size_t temp = n;
    size_t b = 0;
    size_t k_power = 1;
    
    while (temp > MAX_BATCH_SIZE) {
        temp = (temp + k - 1) / k;  // Ceiling division
        b++;
        k_power *= k;
    }
    
    // Now temp <= MAX_BATCH_SIZE after b levels of division by k
    // We need temp to be a power of 2 for the final Waksman shuffle
    size_t a_part = next_power_of_two(temp);
    
    // Calculate m = a_part * k^b
    size_t m = a_part * k_power;
    
    // Ensure m >= n (it should be by construction, but let's be safe)
    if (m < n) {
        // This shouldn't happen, but if it does, we need to increase a_part
        a_part *= 2;
        m = a_part * k_power;
    }
    
    DEBUG_TRACE("Shuffle padding: n=%zu, b=%zu, a_part=%zu, k^b=%zu, m=%zu",
                n, b, a_part, k_power, m);
    
    return m;
}