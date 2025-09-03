#ifndef SHUFFLE_MANAGER_H
#define SHUFFLE_MANAGER_H

#include "../data_structures/table.h"
#include "../data_structures/entry.h"
#include "sgx_urts.h"
#include "../../common/constants.h"
#include "../../common/enclave_types.h"
#include <vector>

/**
 * ShuffleManager - Manages recursive oblivious shuffling for arbitrary-sized vectors
 * 
 * For small vectors (≤ MAX_BATCH_SIZE): Uses 2-way Waksman shuffle directly
 * For large vectors (> MAX_BATCH_SIZE): Uses k-way recursive decomposition
 * 
 * Similar design to MergeSortManager with ocall-based data transfer
 */
class ShuffleManager {
private:
    sgx_enclave_id_t eid;
    
    // For k-way decomposition (large vectors)
    std::vector<std::vector<Entry>> groups;  // K groups after decomposition
    std::vector<size_t> group_positions;     // Current position in each group for reading
    
    // For collecting output during reconstruction
    std::vector<Entry> output_entries;
    
    // Static instance for ocall handling (like MergeSortManager)
    static ShuffleManager* current_instance;
    
public:
    ShuffleManager(sgx_enclave_id_t enclave_id);
    ~ShuffleManager();
    
    // Main shuffle function
    void shuffle(Table& table);
    
    // Ocall handlers for buffered I/O (called from enclave via global C functions)
    static void handle_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size);
    static void handle_refill_from_group(int group_idx, entry_t* buffer, 
                                         size_t buffer_size, size_t* actual_filled);
    static void handle_flush_output(entry_t* buffer, size_t buffer_size);
    
private:
    // Recursive shuffle implementation
    void recursive_shuffle(std::vector<Entry>& entries);
    
    // For small vectors (≤ MAX_BATCH_SIZE)
    void shuffle_small(std::vector<Entry>& entries);
    
    // For large vectors (> MAX_BATCH_SIZE)
    void shuffle_large(std::vector<Entry>& entries);
    
    // Helper to pad to required size
    void pad_entries(std::vector<Entry>& entries, size_t target_size);
    
    // Helper to calculate next power of 2
    static size_t next_power_of_two(size_t n) {
        size_t power = 1;
        while (power < n) power *= 2;
        return power;
    }
    
    // Helper to calculate next multiple of k
    static size_t next_multiple_of_k(size_t n, size_t k) {
        return ((n + k - 1) / k) * k;
    }
    
    // Calculate padding target: smallest m > n where m = 2^a * k^b
    static size_t calculate_shuffle_padding(size_t n);
    
    // Set/clear current instance for ocall handling
    void set_as_current();
    void clear_current();
};

#endif // SHUFFLE_MANAGER_H