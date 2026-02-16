#include "shuffle_manager.h"
#include "debug_util.h"
#include "../core_logic/algorithms/oblivious_waksman.h"
#include <algorithm>
#include <cstring>

// Static member initialization
ShuffleManager* ShuffleManager::current_instance = nullptr;

// Note: ocall handlers removed - no longer needed without enclave

ShuffleManager::ShuffleManager() 
     {
    DEBUG_INFO("ShuffleManager created");
}

ShuffleManager::~ShuffleManager() {
    clear_current();
}

void ShuffleManager::shuffle(Table& table) {
    if (table.size() <= 1) return;
    
    DEBUG_INFO("ShuffleManager::shuffle starting with %zu entries", table.size());
    
    // Verify input is valid size (2^a * k^b)
    if (!Table::is_valid_shuffle_size(table.size())) {
        DEBUG_ERROR("Invalid shuffle size: %zu (not 2^a * k^b format)", table.size());
        // Could throw exception or assert in production
        return;
    }
    
    // Convert table to vector of entries
    std::vector<Entry> entries;
    entries.reserve(table.size());
    for (size_t i = 0; i < table.size(); i++) {
        entries.push_back(table[i]);
    }
    
    // Perform recursive shuffle (input already padded)
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
    size_t n = entries.size();

    DEBUG_INFO("Small shuffle: n=%zu", n);

    // Shuffle directly on the vector data (no conversion needed)
    int result = oblivious_2way_waksman(entries.data(), n);

    if (result != 0) {
        DEBUG_ERROR("Waksman shuffle failed: result=%d", result);
        return;
    }

    DEBUG_INFO("Small shuffle complete: %zu entries", n);
}

void ShuffleManager::shuffle_large(std::vector<Entry>& entries) {
    size_t n = entries.size();

    DEBUG_INFO("Large shuffle: n=%zu", n);

    // Shuffle directly on the vector data (no conversion needed)
    set_as_current();

    int result = oblivious_2way_waksman(entries.data(), n);

    if (result != 0) {
        DEBUG_ERROR("K-way shuffle failed: result=%d", result);
        clear_current();
        return;
    }

    clear_current();

    DEBUG_INFO("Large shuffle complete: %zu entries", n);
}


// Ocall handlers for buffered I/O
void ShuffleManager::handle_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size) {
    if (!current_instance || group_idx < 0 || group_idx >= MERGE_SORT_K) {
        DEBUG_ERROR("Invalid flush_to_group: group_idx=%d, current=%p", 
                    group_idx, current_instance);
        return;
    }
    
    // Append buffer contents to the specified group (direct copy, no conversion)
    auto& group = current_instance->groups[group_idx];
    group.insert(group.end(), buffer, buffer + buffer_size);
    
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
    
    // Fill buffer from current position in group (direct copy, no conversion)
    memcpy(buffer, &group[pos], to_fill * sizeof(entry_t));
    
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
    
    // Append buffer contents to output (direct copy, no conversion)
    auto& output = current_instance->output_entries;
    output.insert(output.end(), buffer, buffer + buffer_size);
    
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

