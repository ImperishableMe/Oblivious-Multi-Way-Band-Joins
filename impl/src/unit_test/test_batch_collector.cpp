/**
 * Unit test for EcallBatchCollector
 * Tests simple linear pass that increments a field by 1
 */

#include <iostream>
#include <vector>
#include "../app/batch/ecall_batch_collector.h"
#include "../app/data_structures/entry.h"
#include "../app/data_structures/table.h"
#include "../app/Enclave_u.h"
#include "sgx_urts.h"
#include "sgx_eid.h"
#include "../common/batch_types.h"

// Simple test transform that increments original_index
sgx_status_t ecall_test_increment_index(sgx_enclave_id_t eid, entry_t* e) {
    e->original_index++;
    return SGX_SUCCESS;
}

int main() {
    // Initialize enclave
    sgx_enclave_id_t eid = 0;
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    // Load enclave
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Error: Failed to initialize enclave (code: " << ret << ")" << std::endl;
        return 1;
    }
    std::cout << "Enclave initialized (ID: " << eid << ")" << std::endl;
    
    // Create a simple table with 10 entries
    Table test_table("test");
    for (int i = 0; i < 10; i++) {
        Entry e;
        e.original_index = i;
        e.local_mult = 1;
        e.final_mult = 1;
        e.join_attr = i * 100;
        e.is_encrypted = 0;  // Make sure entries are not encrypted
        test_table.add_entry(e);
    }
    
    std::cout << "\n=== Initial Table ===" << std::endl;
    for (int i = 0; i < 10; i++) {
        std::cout << "Entry " << i << ": original_index = " << test_table[i].original_index 
                  << ", join_attr = " << test_table[i].join_attr << std::endl;
    }
    
    // Test 1: Simple non-batched linear pass (for comparison)
    std::cout << "\n=== Test 1: Non-batched linear pass ===" << std::endl;
    std::cout << "Incrementing original_index for each pair..." << std::endl;
    
    for (size_t i = 0; i < test_table.size() - 1; i++) {
        entry_t e1 = test_table[i].to_entry_t();
        entry_t e2 = test_table[i+1].to_entry_t();
        
        // Simple operation: e2.original_index = e1.original_index + 1
        ret = ecall_window_set_original_index(eid, &e1, &e2);
        if (ret != SGX_SUCCESS) {
            std::cout << "Error in ecall: " << ret << std::endl;
        }
        
        // Write back
        test_table[i].from_entry_t(e1);
        test_table[i+1].from_entry_t(e2);
    }
    
    std::cout << "After non-batched pass:" << std::endl;
    for (int i = 0; i < 10; i++) {
        std::cout << "Entry " << i << ": original_index = " << test_table[i].original_index << std::endl;
    }
    
    // Reset table
    for (int i = 0; i < 10; i++) {
        test_table[i].original_index = i;
    }
    
    // Test 2: Batched linear pass
    std::cout << "\n=== Test 2: Batched linear pass ===" << std::endl;
    std::cout << "Using EcallBatchCollector..." << std::endl;
    
    EcallBatchCollector collector(eid, OP_ECALL_WINDOW_SET_ORIGINAL_INDEX);
    
    // Add all operations
    for (size_t i = 0; i < test_table.size() - 1; i++) {
        collector.add_operation(test_table[i], test_table[i+1], nullptr);
    }
    
    std::cout << "Added " << test_table.size() - 1 << " operations to batch" << std::endl;
    std::cout << "Flushing batch..." << std::endl;
    
    // Flush - this should write back to original entries
    collector.flush();
    
    std::cout << "After batched pass:" << std::endl;
    for (int i = 0; i < 10; i++) {
        std::cout << "Entry " << i << ": original_index = " << test_table[i].original_index << std::endl;
    }
    
    // Test 3: Simple transform with batched map
    std::cout << "\n=== Test 3: Batched map (set local_mult = 1) ===" << std::endl;
    
    // First, set some different values
    for (int i = 0; i < 10; i++) {
        test_table[i].local_mult = i * 10;
    }
    
    std::cout << "Before batched map:" << std::endl;
    for (int i = 0; i < 10; i++) {
        std::cout << "Entry " << i << ": local_mult = " << test_table[i].local_mult << std::endl;
    }
    
    // Use batched map to set all local_mult to 1
    Table result = test_table.batched_map(eid, OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE, nullptr);
    
    std::cout << "After batched map:" << std::endl;
    for (int i = 0; i < 10; i++) {
        std::cout << "Entry " << i << ": local_mult = " << result[i].local_mult << std::endl;
    }
    
    // Verify results
    bool all_tests_passed = true;
    
    // Check that original_index values are correct after linear pass
    // After window_set_original_index, each entry should have index = previous + 1
    if (test_table[0].original_index != 0) {
        std::cout << "ERROR: Entry 0 has wrong original_index: " << test_table[0].original_index << std::endl;
        all_tests_passed = false;
    }
    for (int i = 1; i < 10; i++) {
        if (test_table[i].original_index != i) {
            std::cout << "ERROR: Entry " << i << " has wrong original_index: " 
                      << test_table[i].original_index << " (expected " << i << ")" << std::endl;
            all_tests_passed = false;
        }
    }
    
    // Check that all local_mult values are 1 after batched map
    for (int i = 0; i < 10; i++) {
        if (result[i].local_mult != 1) {
            std::cout << "ERROR: Entry " << i << " has wrong local_mult: " 
                      << result[i].local_mult << " (expected 1)" << std::endl;
            all_tests_passed = false;
        }
    }
    
    // Cleanup
    sgx_destroy_enclave(eid);
    
    if (all_tests_passed) {
        std::cout << "\n✓ All tests PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ Some tests FAILED!" << std::endl;
        return 1;
    }
}