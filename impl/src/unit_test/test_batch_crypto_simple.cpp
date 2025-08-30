/**
 * Simple unit test for batch encryption/decryption
 * Uses entry_t directly to avoid class/struct mismatch
 */

#include <iostream>
#include <vector>
#include <cstring>
#include "common/types_common.h"
#include "enclave/enclave_types.h"
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

// Test batch dispatcher with encryption
void test_batch_encryption() {
    std::cout << "\n=== Testing Batch Dispatcher Encryption ===" << std::endl;
    
    // Create test data
    const size_t num_entries = 4;
    std::vector<entry_t> entries(num_entries);
    
    // Initialize entries
    for (size_t i = 0; i < num_entries; i++) {
        memset(&entries[i], 0, sizeof(entry_t));
        entries[i].join_attr = (4 - i) * 100;  // 400, 300, 200, 100
        entries[i].original_index = i;
        entries[i].field_type = SOURCE;
        entries[i].is_encrypted = 0;
        entries[i].local_mult = 0;
        entries[i].final_mult = 0;
        
        std::cout << "Entry " << i << ": join_attr=" << entries[i].join_attr 
                  << ", encrypted=" << (int)entries[i].is_encrypted << std::endl;
    }
    
    // Test 1: Encrypt all entries
    std::cout << "\nTest 1: Encrypting entries..." << std::endl;
    for (auto& entry : entries) {
        crypto_status_t ret;
        sgx_status_t ecall_ret = ecall_encrypt_entry(global_eid, &ret, &entry);
        if (ecall_ret != SGX_SUCCESS || ret != CRYPTO_SUCCESS) {
            std::cerr << "Failed to encrypt entry" << std::endl;
            return;
        }
        std::cout << "Encrypted: is_encrypted=" << (int)entry.is_encrypted 
                  << ", nonce=" << entry.nonce << std::endl;
    }
    
    // Test 2: Call batch dispatcher with encrypted entries
    std::cout << "\nTest 2: Calling batch dispatcher with comparator..." << std::endl;
    
    // Create batch operations for simple bubble sort
    std::vector<BatchOperation> operations;
    for (size_t i = 0; i < num_entries - 1; i++) {
        BatchOperation op;
        op.idx1 = i;
        op.idx2 = i + 1;
        op.extra_param = 0;
        operations.push_back(op);
    }
    
    // Call batch dispatcher - should handle decrypt/encrypt internally
    sgx_status_t ret = ecall_batch_dispatcher(
        global_eid,
        entries.data(),
        entries.size(),
        operations.data(),
        operations.size(),
        OP_ECALL_COMPARATOR_JOIN_ATTR
    );
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Batch dispatcher failed: " << ret << std::endl;
        return;
    }
    
    std::cout << "Batch dispatcher completed" << std::endl;
    
    // Test 3: Verify entries are still encrypted
    std::cout << "\nTest 3: Verifying encryption state..." << std::endl;
    bool all_encrypted = true;
    for (size_t i = 0; i < entries.size(); i++) {
        std::cout << "Entry " << i << ": is_encrypted=" << (int)entries[i].is_encrypted << std::endl;
        if (!entries[i].is_encrypted) {
            all_encrypted = false;
        }
    }
    
    if (all_encrypted) {
        std::cout << "✓ All entries remain encrypted after batch operation" << std::endl;
    } else {
        std::cout << "✗ Some entries lost encryption" << std::endl;
    }
    
    // Test 4: Decrypt and verify sort
    std::cout << "\nTest 4: Decrypting and verifying sort..." << std::endl;
    for (auto& entry : entries) {
        crypto_status_t ret;
        sgx_status_t ecall_ret = ecall_decrypt_entry(global_eid, &ret, &entry);
        if (ecall_ret != SGX_SUCCESS || ret != CRYPTO_SUCCESS) {
            std::cerr << "Failed to decrypt entry" << std::endl;
            return;
        }
    }
    
    // Check sort order
    bool sorted = true;
    for (size_t i = 0; i < entries.size(); i++) {
        std::cout << "Entry " << i << ": join_attr=" << entries[i].join_attr 
                  << ", original_index=" << entries[i].original_index << std::endl;
        if (i > 0 && entries[i].join_attr < entries[i-1].join_attr) {
            sorted = false;
        }
    }
    
    if (sorted) {
        std::cout << "✓ Entries correctly sorted" << std::endl;
    } else {
        std::cout << "✗ Entries not properly sorted" << std::endl;
    }
    
    // Test 5: Test with unencrypted entries
    std::cout << "\nTest 5: Testing with unencrypted entries..." << std::endl;
    
    // Reset entries to unencrypted
    for (auto& entry : entries) {
        entry.is_encrypted = 0;
        entry.join_attr = rand() % 1000;
    }
    
    ret = ecall_batch_dispatcher(
        global_eid,
        entries.data(),
        entries.size(),
        operations.data(),
        operations.size(),
        OP_ECALL_COMPARATOR_JOIN_ATTR
    );
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Batch dispatcher failed with unencrypted entries" << std::endl;
        return;
    }
    
    // Verify still unencrypted
    bool all_unencrypted = true;
    for (const auto& entry : entries) {
        if (entry.is_encrypted) {
            all_unencrypted = false;
        }
    }
    
    if (all_unencrypted) {
        std::cout << "✓ Unencrypted entries remain unencrypted" << std::endl;
    } else {
        std::cout << "✗ Some entries were incorrectly encrypted" << std::endl;
    }
}

int main() {
    std::cout << "=== Simple Batch Encryption Test ===" << std::endl;
    
    // Initialize enclave
    if (!initialize_enclave()) {
        return 1;
    }
    
    // Run test
    test_batch_encryption();
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    std::cout << "\nTest completed." << std::endl;
    
    return 0;
}