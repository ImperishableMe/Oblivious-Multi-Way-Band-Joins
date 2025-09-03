#include "merge_sort_manager.h"
#include "debug_util.h"
#include "Enclave_u.h"
#include <algorithm>
#include <cmath>

// Static member initialization
MergeSortManager* MergeSortManager::current_instance = nullptr;

// Global ocall handler function (must be extern "C" for EDL)
extern "C" void ocall_refill_buffer(int buffer_idx, entry_t* buffer, 
                                    size_t buffer_size, size_t* actual_filled) {
    MergeSortManager::handle_refill_buffer(buffer_idx, buffer, buffer_size, actual_filled);
}

MergeSortManager::MergeSortManager(sgx_enclave_id_t enclave_id, OpEcall type)
    : eid(enclave_id), comparator_type(type) {
    DEBUG_INFO("MergeSortManager created with comparator type %d", type);
}

MergeSortManager::~MergeSortManager() {
    clear_current();
}

void MergeSortManager::set_as_current() {
    current_instance = this;
}

void MergeSortManager::clear_current() {
    if (current_instance == this) {
        current_instance = nullptr;
    }
}

void MergeSortManager::handle_refill_buffer(int buffer_idx, entry_t* buffer, 
                                           size_t buffer_size, size_t* actual_filled) {
    if (!current_instance || buffer_idx < 0 || 
        buffer_idx >= (int)current_instance->runs.size()) {
        *actual_filled = 0;
        return;
    }
    
    auto& run = current_instance->runs[buffer_idx];
    auto& pos = current_instance->run_positions[buffer_idx];
    
    // Calculate how many entries to copy
    size_t remaining = run.size() - pos;
    size_t to_copy = std::min(buffer_size, remaining);
    
    // Copy entries to buffer
    for (size_t i = 0; i < to_copy; i++) {
        buffer[i] = run[pos + i].to_entry_t();
    }
    
    pos += to_copy;
    *actual_filled = to_copy;
    
    DEBUG_TRACE("Refilled buffer %d with %zu entries (pos now %zu/%zu)", 
                buffer_idx, to_copy, pos, run.size());
}

void MergeSortManager::sort(Table& table) {
    if (table.size() <= 1) {
        return;  // Already sorted
    }
    
    DEBUG_INFO("Starting merge sort on table with %zu entries", table.size());
    
    // Phase 1: Create initial sorted runs
    create_sorted_runs(table);
    
    // Phase 2: Merge runs recursively
    merge_runs_recursive();
    
    // Copy final result back to table
    if (runs.size() == 1) {
        table.clear();
        for (const auto& entry : runs[0]) {
            table.add_entry(entry);
        }
        DEBUG_INFO("Merge sort complete, table has %zu sorted entries", table.size());
    } else {
        DEBUG_ERROR("Merge sort failed - expected 1 run, got %zu", runs.size());
    }
}

void MergeSortManager::create_sorted_runs(Table& table) {
    size_t run_size = MAX_BATCH_SIZE;  // Maximum entries per run
    size_t num_runs = (table.size() + run_size - 1) / run_size;
    
    DEBUG_INFO("Creating %zu sorted runs of size %zu", num_runs, run_size);
    
    runs.clear();
    runs.reserve(num_runs);
    
    for (size_t i = 0; i < num_runs; i++) {
        size_t start = i * run_size;
        size_t end = std::min(start + run_size, table.size());
        size_t count = end - start;
        
        // Extract run from table
        std::vector<Entry> run;
        run.reserve(count);
        for (size_t j = start; j < end; j++) {
            run.push_back(table[j]);
        }
        
        // Sort this run in enclave
        sort_run_in_enclave(run);
        
        // Add to runs
        runs.push_back(std::move(run));
    }
    
    DEBUG_INFO("Created %zu sorted runs", runs.size());
}

void MergeSortManager::sort_run_in_enclave(std::vector<Entry>& entries) {
    if (entries.empty()) {
        return;
    }
    
    size_t size = entries.size();
    DEBUG_TRACE("Sorting run of %zu entries in enclave", size);
    
    // Convert to entry_t array
    std::vector<entry_t> entry_array(size);
    for (size_t i = 0; i < size; i++) {
        entry_array[i] = entries[i].to_entry_t();
    }
    
    // Call enclave to sort
    sgx_status_t retval;
    sgx_status_t status = ecall_heap_sort(eid, &retval, entry_array.data(), size, 
                                          (int)comparator_type);
    
    if (status != SGX_SUCCESS) {
        DEBUG_ERROR("ecall_heap_sort failed with status %d", status);
        return;
    }
    
    // Convert back to Entry objects
    for (size_t i = 0; i < size; i++) {
        entries[i].from_entry_t(entry_array[i]);
    }
    
    DEBUG_TRACE("Run sorted successfully");
}

void MergeSortManager::merge_runs_recursive() {
    while (runs.size() > 1) {
        std::vector<std::vector<Entry>> new_runs;
        
        // Merge runs in groups of k
        for (size_t i = 0; i < runs.size(); i += MERGE_SORT_K) {
            size_t end = std::min(i + MERGE_SORT_K, runs.size());
            std::vector<size_t> run_indices;
            
            for (size_t j = i; j < end; j++) {
                run_indices.push_back(j);
            }
            
            // Merge these k (or fewer) runs
            std::vector<Entry> merged = k_way_merge(run_indices);
            new_runs.push_back(std::move(merged));
        }
        
        DEBUG_INFO("Merged %zu runs into %zu runs", runs.size(), new_runs.size());
        runs = std::move(new_runs);
    }
}

std::vector<Entry> MergeSortManager::k_way_merge(const std::vector<size_t>& run_indices) {
    size_t k = run_indices.size();
    if (k == 0) {
        return {};
    }
    
    if (k == 1) {
        // Single run, just return it
        return runs[run_indices[0]];
    }
    
    DEBUG_TRACE("Starting k-way merge with k=%zu", k);
    
    // Set up for ocall handling
    set_as_current();
    
    // Reset run positions
    run_positions.clear();
    run_positions.resize(runs.size(), 0);
    
    // Initialize merge in enclave
    sgx_status_t retval;
    sgx_status_t status = ecall_k_way_merge_init(eid, &retval, k, (int)comparator_type);
    if (status != SGX_SUCCESS || retval != SGX_SUCCESS) {
        DEBUG_ERROR("ecall_k_way_merge_init failed with status %d, retval %d", status, retval);
        clear_current();
        return {};
    }
    
    // Collect merged output
    std::vector<Entry> result;
    std::vector<entry_t> output_buffer(MERGE_BUFFER_SIZE);
    
    int merge_complete = 0;
    while (!merge_complete) {
        size_t output_produced = 0;
        
        status = ecall_k_way_merge_process(eid, &retval, output_buffer.data(), 
                                          MERGE_BUFFER_SIZE,
                                          &output_produced, &merge_complete);
        
        if (status != SGX_SUCCESS) {
            DEBUG_ERROR("ecall_k_way_merge_process failed with status %d", status);
            break;
        }
        
        // Add produced entries to result
        for (size_t i = 0; i < output_produced; i++) {
            Entry e;
            e.from_entry_t(output_buffer[i]);
            result.push_back(e);
        }
        
        DEBUG_TRACE("Merge produced %zu entries, complete=%d", output_produced, merge_complete);
    }
    
    // Clean up merge state
    ecall_k_way_merge_cleanup(eid, &retval);
    clear_current();
    
    DEBUG_TRACE("K-way merge complete, produced %zu entries", result.size());
    return result;
}