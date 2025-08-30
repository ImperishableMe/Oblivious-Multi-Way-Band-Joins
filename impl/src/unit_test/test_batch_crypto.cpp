/**
 * Unit test for batch encryption/decryption in dispatcher
 * Tests that the batch dispatcher correctly handles encryption
 */

#include <iostream>
#include <vector>
#include <cstring>
#include "app/data_structures/entry.h"
#include "app/batch/ecall_batch_collector.h"
#include "common/batch_types.h"
#include "common/types_common.h"
#include "sgx_urts.h"
#include "sgx_eid.h"
#include "app/Enclave_u.h"

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

// Initialize SGX enclave
bool initialize_enclave() {
    sgx_launch_token_t token = {0};
    int token_updated = 0;
    sgx_status_t ret = sgx_create_enclave(
        "enclave.signed.so",
        SGX_DEBUG_FLAG,
        &token,
        &token_updated,
        &global_eid,
        NULL
    );
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error: " << ret << std::endl;
        return false;
    }
    
    std::cout << "Enclave created successfully (ID: " << global_eid << ")" << std::endl;
    return true;
}

// Test comparator with encrypted entries
void test_encrypted_comparator() {
    std::cout << "\n=== Testing Batch Encryption with Comparator ===" << std::endl;
    
    // Create test entries
    std::vector<Entry> entries(4);
    
    // Initialize entries with test data
    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].join_attr = (4 - i) * 100;  // 400, 300, 200, 100
        entries[i].original_index = i;
        entries[i].field_type = SOURCE;
        entries[i].is_encrypted = 0;  // Start unencrypted
        
        // Set some attributes
        for (int j = 0; j < 3; j++) {
            entries[i].attributes[j] = i * 10 + j;
        }
        
        std::cout << "Entry " << i << ": join_attr=" << entries[i].join_attr 
                  << ", encrypted=" << (int)entries[i].is_encrypted << std::endl;
    }
    
    // First, encrypt all entries using individual ecalls
    std::cout << "\nEncrypting entries..." << std::endl;
    for (auto& entry : entries) {
        crypto_status_t ret;
        entry_t* entry_ptr = reinterpret_cast<entry_t*>(&entry);
        sgx_status_t ecall_ret = ecall_encrypt_entry(global_eid, &ret, entry_ptr);
        if (ecall_ret != SGX_SUCCESS || ret != CRYPTO_SUCCESS) {
            std::cerr << "Failed to encrypt entry" << std::endl;
            return;
        }
        std::cout << "Entry encrypted: is_encrypted=" << (int)entry.is_encrypted 
                  << ", nonce=" << entry.nonce << std::endl;
    }
    
    // Now test batch comparator with encrypted entries
    std::cout << "\nTesting batch comparator sort with encrypted entries..." << std::endl;
    
    // Create batch collector for join_attr comparator
    EcallBatchCollector collector(global_eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    
    // Simple bubble sort using batched comparator
    for (size_t i = 0; i < entries.size(); i++) {
        for (size_t j = 0; j < entries.size() - i - 1; j++) {
            collector.add_operation(entries[j], entries[j + 1]);
        }
    }
    
    // Flush the batch - this should handle decryption/encryption internally
    collector.flush();
    auto stats = collector.get_stats();
    std::cout << "Batch stats: " << stats.total_operations << " operations in " 
              << stats.total_flushes << " flushes" << std::endl;
    
    // Verify entries are still encrypted
    std::cout << "\nAfter batch operation:" << std::endl;
    for (size_t i = 0; i < entries.size(); i++) {
        std::cout << "Entry " << i << ": is_encrypted=" << (int)entries[i].is_encrypted << std::endl;
    }
    
    // Decrypt to verify sort worked
    std::cout << "\nDecrypting to verify sort..." << std::endl;
    for (auto& entry : entries) {
        crypto_status_t ret;
        entry_t* entry_ptr = reinterpret_cast<entry_t*>(&entry);
        sgx_status_t ecall_ret = ecall_decrypt_entry(global_eid, &ret, entry_ptr);
        if (ecall_ret != SGX_SUCCESS || ret != CRYPTO_SUCCESS) {
            std::cerr << "Failed to decrypt entry" << std::endl;
            return;
        }
    }
    
    // Check if sorted
    std::cout << "\nFinal sorted order:" << std::endl;
    bool sorted = true;
    for (size_t i = 0; i < entries.size(); i++) {
        std::cout << "Entry " << i << ": join_attr=" << entries[i].join_attr 
                  << ", original_index=" << entries[i].original_index << std::endl;
        if (i > 0 && entries[i].join_attr < entries[i-1].join_attr) {
            sorted = false;
        }
    }
    
    if (sorted) {
        std::cout << "✓ Test PASSED: Entries correctly sorted while maintaining encryption" << std::endl;
    } else {
        std::cout << "✗ Test FAILED: Entries not properly sorted" << std::endl;
    }
}

// Test transform operations with encryption
void test_encrypted_transform() {
    std::cout << "\n=== Testing Batch Encryption with Transform ===" << std::endl;
    
    // Create test entries
    std::vector<Entry> entries(4);
    
    // Initialize and encrypt entries
    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].local_mult = 0;
        entries[i].final_mult = 0;
        entries[i].is_encrypted = 0;
        
        // Encrypt the entry
        crypto_status_t ret;
        entry_t* entry_ptr = reinterpret_cast<entry_t*>(&entries[i]);
        sgx_status_t ecall_ret = ecall_encrypt_entry(global_eid, &ret, entry_ptr);
        if (ecall_ret != SGX_SUCCESS || ret != CRYPTO_SUCCESS) {
            std::cerr << "Failed to encrypt entry" << std::endl;
            return;
        }
    }
    
    std::cout << "Entries encrypted, applying transform..." << std::endl;
    
    // Test batched transform operation
    EcallBatchCollector collector(global_eid, OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE);
    
    for (auto& entry : entries) {
        collector.add_operation(entry, 0);  // Transform operations use single entry
    }
    
    collector.flush();
    
    // Decrypt and verify
    std::cout << "\nVerifying transform results..." << std::endl;
    bool all_correct = true;
    for (auto& entry : entries) {
        crypto_status_t ret;
        entry_t* entry_ptr = reinterpret_cast<entry_t*>(&entry);
        sgx_status_t ecall_ret = ecall_decrypt_entry(global_eid, &ret, entry_ptr);
        if (ecall_ret != SGX_SUCCESS || ret != CRYPTO_SUCCESS) {
            std::cerr << "Failed to decrypt entry" << std::endl;
            return;
        }
        
        std::cout << "Entry: local_mult=" << entry.local_mult 
                  << ", final_mult=" << entry.final_mult << std::endl;
        
        if (entry.local_mult != 1 || entry.final_mult != 0) {
            all_correct = false;
        }
    }
    
    if (all_correct) {
        std::cout << "✓ Test PASSED: Transform correctly applied with encryption" << std::endl;
    } else {
        std::cout << "✗ Test FAILED: Transform not properly applied" << std::endl;
    }
}

int main() {
    std::cout << "=== Batch Encryption Unit Test ===" << std::endl;
    
    // Initialize enclave
    if (!initialize_enclave()) {
        return 1;
    }
    
    // Run tests
    test_encrypted_comparator();
    test_encrypted_transform();
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    std::cout << "\nTests completed." << std::endl;
    
    return 0;
}