/**
 * Test program to measure SGX ecall overhead components
 * Breaks down overhead into:
 * 1. Entry<->entry_t conversion
 * 2. Data marshalling/copying
 * 3. SGX ecall transition
 * 4. Actual operation time
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <random>
#include "../app/data_structures/entry.h"
#include "../app/data_structures/table.h"
#include "../app/crypto/crypto_utils.h"
#include "../app/batch/ecall_batch_collector.h"
#include "../app/counted_ecalls.h"
#include "../app/Enclave_u.h"
#include "sgx_urts.h"

using namespace std::chrono;

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

// Initialize SGX enclave
bool initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    // Create enclave
    ret = sgx_create_enclave("../enclave.signed.so", SGX_DEBUG_FLAG, 
                            nullptr, nullptr, &global_eid, nullptr);
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error: " << ret << std::endl;
        return false;
    }
    
    std::cout << "Enclave initialized successfully" << std::endl;
    return true;
}

// Test 1: Measure Entry->entry_t conversion overhead
void test_conversion_overhead(size_t num_entries) {
    std::cout << "\n=== Test 1: Entry<->entry_t Conversion Overhead ===" << std::endl;
    std::cout << "Converting " << num_entries << " entries..." << std::endl;
    
    // Create test entries
    std::vector<Entry> entries(num_entries);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000000);
    
    // Initialize entries with random data
    for (size_t i = 0; i < num_entries; i++) {
        entries[i].join_attr = dis(gen);
        entries[i].original_index = i;
        for (int j = 0; j < 10; j++) {  // Fill some attributes
            entries[i].attributes[j] = dis(gen);
        }
    }
    
    // Measure Entry -> entry_t conversion
    auto start = high_resolution_clock::now();
    std::vector<entry_t> c_entries;
    c_entries.reserve(num_entries);
    for (const auto& entry : entries) {
        c_entries.push_back(entry.to_entry_t());
    }
    auto end = high_resolution_clock::now();
    
    auto conversion_to_time = duration_cast<microseconds>(end - start).count();
    std::cout << "Entry->entry_t conversion: " << conversion_to_time << " μs" 
              << " (" << (double)conversion_to_time/num_entries << " μs per entry)" << std::endl;
    
    // Measure entry_t -> Entry conversion
    start = high_resolution_clock::now();
    std::vector<Entry> converted_entries;
    converted_entries.reserve(num_entries);
    for (const auto& c_entry : c_entries) {
        Entry e;
        e.from_entry_t(c_entry);
        converted_entries.push_back(e);
    }
    end = high_resolution_clock::now();
    
    auto conversion_from_time = duration_cast<microseconds>(end - start).count();
    std::cout << "entry_t->Entry conversion: " << conversion_from_time << " μs"
              << " (" << (double)conversion_from_time/num_entries << " μs per entry)" << std::endl;
    
    std::cout << "Total conversion overhead: " << (conversion_to_time + conversion_from_time) << " μs" << std::endl;
}

// Test 2: Simple ecall with no operation (measure pure SGX transition)
void test_noop_ecall_overhead(size_t num_entries) {
    std::cout << "\n=== Test 2: Pure SGX Ecall Transition Overhead ===" << std::endl;
    std::cout << "Testing with " << num_entries << " entries..." << std::endl;
    
    // Create and initialize entries
    std::vector<entry_t> entries(num_entries);
    for (size_t i = 0; i < num_entries; i++) {
        entries[i].join_attr = i;
        entries[i].original_index = i;
        entries[i].is_encrypted = 0;  // Not encrypted for this test
    }
    
    // Create batch operations (simple comparisons)
    std::vector<BatchOperation> ops;
    for (size_t i = 0; i < num_entries - 1; i += 2) {
        BatchOperation op;
        op.idx1 = i;
        op.idx2 = i + 1;
        for (int j = 0; j < MAX_EXTRA_PARAMS; j++) {
            op.extra_params[j] = BATCH_NO_PARAM;
        }
        ops.push_back(op);
    }
    
    // Test single large batch ecall
    auto start = high_resolution_clock::now();
    
    sgx_status_t status = ecall_batch_dispatcher(
        global_eid,
        entries.data(),
        entries.size(),
        ops.data(),
        ops.size(),
        ops.size() * sizeof(BatchOperation),
        OP_ECALL_COMPARATOR_JOIN_ATTR
    );
    
    auto end = high_resolution_clock::now();
    
    if (status != SGX_SUCCESS) {
        std::cerr << "Ecall failed with status: " << status << std::endl;
        return;
    }
    
    auto ecall_time = duration_cast<microseconds>(end - start).count();
    std::cout << "Single batch ecall (" << ops.size() << " operations): " << ecall_time << " μs" << std::endl;
    std::cout << "Per operation: " << (double)ecall_time/ops.size() << " μs" << std::endl;
    
    // Test multiple smaller batch ecalls for comparison
    size_t small_batch_size = 100;
    start = high_resolution_clock::now();
    
    for (size_t i = 0; i < ops.size(); i += small_batch_size) {
        size_t batch_end = std::min(i + small_batch_size, ops.size());
        size_t batch_ops_count = batch_end - i;
        
        status = ecall_batch_dispatcher(
            global_eid,
            entries.data(),
            entries.size(),
            ops.data() + i,
            batch_ops_count,
            batch_ops_count * sizeof(BatchOperation),
            OP_ECALL_COMPARATOR_JOIN_ATTR
        );
        
        if (status != SGX_SUCCESS) {
            std::cerr << "Small batch ecall failed" << std::endl;
            return;
        }
    }
    
    end = high_resolution_clock::now();
    
    auto small_batch_time = duration_cast<microseconds>(end - start).count();
    size_t num_small_batches = (ops.size() + small_batch_size - 1) / small_batch_size;
    std::cout << "\n" << num_small_batches << " small batches (" << small_batch_size 
              << " ops each): " << small_batch_time << " μs" << std::endl;
    std::cout << "Per batch: " << (double)small_batch_time/num_small_batches << " μs" << std::endl;
    std::cout << "Per operation: " << (double)small_batch_time/ops.size() << " μs" << std::endl;
    
    std::cout << "\nSpeedup from batching: " << (double)small_batch_time/ecall_time << "x" << std::endl;
}

// Test 3: Measure batch collector overhead
void test_batch_collector_overhead(size_t num_entries) {
    std::cout << "\n=== Test 3: Batch Collector Infrastructure Overhead ===" << std::endl;
    std::cout << "Testing with " << num_entries << " entries..." << std::endl;
    
    // Create test entries
    std::vector<Entry> entries(num_entries);
    for (size_t i = 0; i < num_entries; i++) {
        entries[i].join_attr = i;
        entries[i].original_index = i;
        entries[i].is_encrypted = false;
    }
    
    // Test with batch collector
    auto start = high_resolution_clock::now();
    
    EcallBatchCollector collector(global_eid, OP_ECALL_COMPARATOR_JOIN_ATTR, 2000);
    
    // Add operations
    for (size_t i = 0; i < num_entries - 1; i += 2) {
        collector.add_operation(entries[i], entries[i + 1]);
    }
    
    // Force flush
    collector.flush();
    
    auto end = high_resolution_clock::now();
    
    auto collector_time = duration_cast<microseconds>(end - start).count();
    std::cout << "Batch collector total time: " << collector_time << " μs" << std::endl;
    std::cout << "Per operation: " << (double)collector_time/(num_entries/2) << " μs" << std::endl;
    
    // Get statistics
    auto stats = collector.get_stats();
    std::cout << "\nBatch collector statistics:" << std::endl;
    std::cout << "  Total operations: " << stats.total_operations << std::endl;
    std::cout << "  Total flushes: " << stats.total_flushes << std::endl;
    std::cout << "  Entries processed: " << stats.total_entries_processed << std::endl;
    std::cout << "  Max batch size: " << stats.max_batch_size_reached << std::endl;
}

// Test 4: Measure encryption/decryption overhead
void test_encryption_overhead(size_t num_entries) {
    std::cout << "\n=== Test 4: Encryption/Decryption Overhead ===" << std::endl;
    std::cout << "Testing with " << num_entries << " entries..." << std::endl;
    
    // Create test entries
    std::vector<Entry> entries(num_entries);
    for (size_t i = 0; i < num_entries; i++) {
        entries[i].join_attr = i;
        entries[i].original_index = i;
        entries[i].is_encrypted = false;
        entries[i].nonce = 0;
    }
    
    // Measure encryption time
    auto start = high_resolution_clock::now();
    
    for (auto& entry : entries) {
        CryptoUtils::encrypt_entry(entry, global_eid);
    }
    
    auto end = high_resolution_clock::now();
    
    auto encrypt_time = duration_cast<microseconds>(end - start).count();
    std::cout << "Encryption time: " << encrypt_time << " μs"
              << " (" << (double)encrypt_time/num_entries << " μs per entry)" << std::endl;
    
    // Measure decryption time
    start = high_resolution_clock::now();
    
    for (auto& entry : entries) {
        CryptoUtils::decrypt_entry(entry, global_eid);
    }
    
    end = high_resolution_clock::now();
    
    auto decrypt_time = duration_cast<microseconds>(end - start).count();
    std::cout << "Decryption time: " << decrypt_time << " μs"
              << " (" << (double)decrypt_time/num_entries << " μs per entry)" << std::endl;
    
    std::cout << "Total crypto overhead: " << (encrypt_time + decrypt_time) << " μs" << std::endl;
}

int main(int argc, char* argv[]) {
    size_t num_entries = 2000;  // Default to batch size
    
    if (argc > 1) {
        num_entries = std::stoul(argv[1]);
    }
    
    std::cout << "SGX Overhead Measurement Test" << std::endl;
    std::cout << "==============================" << std::endl;
    
    // Initialize enclave
    if (!initialize_enclave()) {
        return 1;
    }
    
    // Run tests
    test_conversion_overhead(num_entries);
    test_noop_ecall_overhead(num_entries);
    test_batch_collector_overhead(num_entries);
    test_encryption_overhead(num_entries);
    
    // Summary
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Tested with " << num_entries << " entries" << std::endl;
    std::cout << "Entry size: " << sizeof(Entry) << " bytes" << std::endl;
    std::cout << "entry_t size: " << sizeof(entry_t) << " bytes" << std::endl;
    std::cout << "Total data size: " << num_entries * sizeof(entry_t) << " bytes ("
              << (num_entries * sizeof(entry_t)) / 1024.0 << " KB)" << std::endl;
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    
    return 0;
}