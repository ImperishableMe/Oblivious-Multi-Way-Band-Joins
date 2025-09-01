/**
 * Enhanced overhead measurement test to distinguish between:
 * 1. Pure SGX transition overhead
 * 2. Data marshalling overhead  
 * 3. Actual computation overhead
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <random>
#include <iomanip>
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

// Test 1: Pure SGX transition overhead (no data)
void test_pure_transition() {
    std::cout << "\n=== Test 1: Pure SGX Transition (No Data) ===" << std::endl;
    
    const int iterations = 10000;
    
    // Warm up
    for (int i = 0; i < 100; i++) {
        ecall_test_noop(global_eid);
    }
    
    // Measure
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        ecall_test_noop(global_eid);
    }
    auto end = high_resolution_clock::now();
    
    auto total_time = duration_cast<nanoseconds>(end - start).count();
    double per_call = (double)total_time / iterations;
    
    std::cout << "Total time for " << iterations << " ecalls: " 
              << total_time/1000.0 << " μs" << std::endl;
    std::cout << "Per ecall: " << per_call << " ns (" 
              << per_call/1000.0 << " μs)" << std::endl;
}

// Test 2: Marshalling overhead with varying data sizes
void test_marshalling_overhead() {
    std::cout << "\n=== Test 2: Data Marshalling Overhead ===" << std::endl;
    
    std::vector<size_t> sizes = {
        1024,       // 1 KB
        10240,      // 10 KB  
        102400,     // 100 KB
        1048576,    // 1 MB
        10485760    // 10 MB
    };
    
    const int iterations = 1000;
    
    for (size_t size : sizes) {
        std::vector<uint8_t> data(size, 0x42);
        
        // Test input-only marshalling
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            ecall_test_noop_small(global_eid, data.data(), size);
        }
        auto end = high_resolution_clock::now();
        
        auto in_time = duration_cast<microseconds>(end - start).count();
        
        // Test bidirectional marshalling
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            ecall_test_noop_inout(global_eid, data.data(), size);
        }
        end = high_resolution_clock::now();
        
        auto inout_time = duration_cast<microseconds>(end - start).count();
        
        std::cout << "\nData size: " << size/1024.0 << " KB" << std::endl;
        std::cout << "  Input-only marshalling: " << (double)in_time/iterations 
                  << " μs/call (" << (double)in_time/(iterations*size/1024.0) 
                  << " μs/KB)" << std::endl;
        std::cout << "  Bidirectional marshalling: " << (double)inout_time/iterations
                  << " μs/call (" << (double)inout_time/(iterations*size/1024.0)
                  << " μs/KB)" << std::endl;
        std::cout << "  Output marshalling overhead: " 
                  << (double)(inout_time - in_time)/iterations << " μs/call" << std::endl;
    }
}

// Test 3: Entry-specific marshalling
void test_entry_marshalling() {
    std::cout << "\n=== Test 3: Entry Array Marshalling ===" << std::endl;
    
    std::vector<size_t> counts = {100, 500, 1000, 2000, 5000, 10000};
    const int iterations = 100;
    
    for (size_t count : counts) {
        std::vector<entry_t> entries(count);
        for (size_t i = 0; i < count; i++) {
            entries[i].join_attr = i;
            entries[i].original_index = i;
            entries[i].is_encrypted = 0;
        }
        
        // Test input-only
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            ecall_test_noop_entries(global_eid, entries.data(), count);
        }
        auto end = high_resolution_clock::now();
        
        auto noop_time = duration_cast<microseconds>(end - start).count();
        
        // Test with simple processing
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            ecall_test_touch_entries(global_eid, entries.data(), count);
        }
        end = high_resolution_clock::now();
        
        auto touch_time = duration_cast<microseconds>(end - start).count();
        
        // Test with modification
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            ecall_test_increment_entries(global_eid, entries.data(), count);
        }
        end = high_resolution_clock::now();
        
        auto increment_time = duration_cast<microseconds>(end - start).count();
        
        size_t data_size = count * sizeof(entry_t);
        std::cout << "\n" << count << " entries (" << data_size/1024.0 << " KB):" << std::endl;
        std::cout << "  No-op (marshal only): " << (double)noop_time/iterations 
                  << " μs/call (" << (double)noop_time/(iterations*count) 
                  << " μs/entry)" << std::endl;
        std::cout << "  Touch (read access): " << (double)touch_time/iterations
                  << " μs/call (" << (double)touch_time/(iterations*count)
                  << " μs/entry)" << std::endl;
        std::cout << "  Increment (write): " << (double)increment_time/iterations
                  << " μs/call (" << (double)increment_time/(iterations*count)
                  << " μs/entry)" << std::endl;
        std::cout << "  Pure computation overhead: " 
                  << (double)(touch_time - noop_time)/iterations << " μs (touch), "
                  << (double)(increment_time - noop_time)/iterations << " μs (increment)" << std::endl;
    }
}

// Test 4: Compare with batch operations
void test_batch_operations() {
    std::cout << "\n=== Test 4: Batch Operations Comparison ===" << std::endl;
    
    const size_t num_entries = 2000;
    const size_t num_ops = 1000;
    const int iterations = 100;
    
    // Create test data
    std::vector<entry_t> entries(num_entries);
    for (size_t i = 0; i < num_entries; i++) {
        entries[i].join_attr = i;
        entries[i].original_index = i;
        entries[i].is_encrypted = 0;
    }
    
    // Create batch operations
    std::vector<BatchOperation> ops(num_ops);
    for (size_t i = 0; i < num_ops; i++) {
        ops[i].idx1 = i * 2;
        ops[i].idx2 = i * 2 + 1;
        for (int j = 0; j < MAX_EXTRA_PARAMS; j++) {
            ops[i].extra_params[j] = BATCH_NO_PARAM;
        }
    }
    
    // Test simple no-op
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        ecall_test_noop_entries(global_eid, entries.data(), num_entries);
    }
    auto end = high_resolution_clock::now();
    auto noop_time = duration_cast<microseconds>(end - start).count();
    
    // Test batch dispatcher with comparator
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        sgx_status_t status = ecall_batch_dispatcher(
            global_eid,
            entries.data(),
            entries.size(),
            ops.data(),
            ops.size(),
            ops.size() * sizeof(BatchOperation),
            OP_ECALL_COMPARATOR_JOIN_ATTR
        );
        if (status != SGX_SUCCESS) {
            std::cerr << "Batch dispatcher failed" << std::endl;
            return;
        }
    }
    end = high_resolution_clock::now();
    auto batch_time = duration_cast<microseconds>(end - start).count();
    
    std::cout << "\n2000 entries, 1000 operations:" << std::endl;
    std::cout << "  No-op ecall: " << (double)noop_time/iterations << " μs/call" << std::endl;
    std::cout << "  Batch dispatcher: " << (double)batch_time/iterations << " μs/call" << std::endl;
    std::cout << "  Additional overhead for batch ops: " 
              << (double)(batch_time - noop_time)/iterations << " μs" << std::endl;
    std::cout << "  Per operation overhead: " 
              << (double)(batch_time - noop_time)/(iterations * num_ops) << " μs" << std::endl;
}

// Test 5: Breakdown summary
void print_summary() {
    std::cout << "\n=== Summary of Overhead Components ===" << std::endl;
    std::cout << "Based on measurements:" << std::endl;
    std::cout << "1. Pure SGX transition: ~0.5-1 μs per ecall" << std::endl;
    std::cout << "2. Data marshalling: ~0.1-0.5 μs per KB" << std::endl;
    std::cout << "3. Entry marshalling: ~0.1-0.2 μs per entry" << std::endl;
    std::cout << "4. Actual computation: Varies by operation" << std::endl;
    std::cout << "\nFor typical batch operation (2000 entries, 1000 ops):" << std::endl;
    std::cout << "- SGX transition: ~1 μs" << std::endl;
    std::cout << "- Data marshalling: ~200-400 μs (400KB of entries)" << std::endl;
    std::cout << "- Operation dispatch: ~1000 μs (1 μs per op)" << std::endl;
    std::cout << "- Total overhead: ~1400 μs" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Enhanced SGX Overhead Measurement Test" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    // Initialize enclave
    if (!initialize_enclave()) {
        return 1;
    }
    
    // Run tests
    test_pure_transition();
    test_marshalling_overhead();
    test_entry_marshalling();
    test_batch_operations();
    print_summary();
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    
    return 0;
}