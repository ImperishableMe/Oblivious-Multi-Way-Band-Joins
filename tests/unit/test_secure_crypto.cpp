/**
 * Test secure encryption/decryption functionality
 */

#include <iostream>
#include <cstring>
#include "sgx_urts.h"
#include "../app/Enclave_u.h"
#include "../enclave/enclave_types.h"

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

void print_entry(const entry_t& entry, const std::string& label) {
    std::cout << label << ":\n";
    std::cout << "  join_attr: " << entry.join_attr << "\n";
    std::cout << "  original_index: " << entry.original_index << "\n";
    std::cout << "  is_encrypted: " << (int)entry.is_encrypted << "\n";
    std::cout << "  attributes[0-4]: ";
    for (int i = 0; i < 5; i++) {
        std::cout << entry.attributes[i] << " ";
    }
    std::cout << "\n";
}

int main() {
    // Initialize enclave
    sgx_status_t ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, 
                                          NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: " << ret << std::endl;
        return 1;
    }
    
    std::cout << "Enclave initialized\n\n";
    
    // Create a test entry
    entry_t original;
    memset(&original, 0, sizeof(entry_t));
    
    original.join_attr = 12345;
    original.original_index = 42;
    original.local_mult = 100;
    original.final_mult = 200;
    original.is_encrypted = 0;
    
    // Set some test attributes (integers now)
    original.attributes[0] = 100;
    original.attributes[1] = 200;
    original.attributes[2] = 300;
    original.attributes[3] = 400;
    original.attributes[4] = 500;
    
    // Set column names
    strcpy(original.column_names[0], "COL1");
    strcpy(original.column_names[1], "COL2");
    strcpy(original.column_names[2], "COL3");
    strcpy(original.column_names[3], "COL4");
    strcpy(original.column_names[4], "COL5");
    
    print_entry(original, "Original entry");
    
    // Make a copy for testing
    entry_t encrypted = original;
    
    // Test secure encryption
    std::cout << "\n=== Testing secure encryption ===\n";
    crypto_status_t crypto_ret;
    ret = ecall_encrypt_entry(global_eid, &crypto_ret, &encrypted);
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "ECALL failed: " << ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    
    if (crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Encryption failed: " << crypto_ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    
    print_entry(encrypted, "After encryption");
    
    // Check that values changed
    bool values_changed = false;
    if (encrypted.join_attr != original.join_attr ||
        encrypted.original_index != original.original_index ||
        encrypted.attributes[0] != original.attributes[0]) {
        values_changed = true;
    }
    
    std::cout << "\nValues changed after encryption: " << (values_changed ? "YES" : "NO") << "\n";
    
    // Test secure decryption
    std::cout << "\n=== Testing secure decryption ===\n";
    entry_t decrypted = encrypted;
    
    ret = ecall_decrypt_entry(global_eid, &crypto_ret, &decrypted);
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "ECALL failed: " << ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    
    if (crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Decryption failed: " << crypto_ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    
    print_entry(decrypted, "After decryption");
    
    // Verify values match original
    bool matches = true;
    if (decrypted.join_attr != original.join_attr) {
        std::cout << "ERROR: join_attr mismatch! " << decrypted.join_attr << " != " << original.join_attr << "\n";
        matches = false;
    }
    if (decrypted.original_index != original.original_index) {
        std::cout << "ERROR: original_index mismatch! " << decrypted.original_index << " != " << original.original_index << "\n";
        matches = false;
    }
    if (decrypted.attributes[0] != original.attributes[0]) {
        std::cout << "ERROR: attributes[0] mismatch! " << decrypted.attributes[0] << " != " << original.attributes[0] << "\n";
        matches = false;
    }
    
    std::cout << "\nDecrypted values match original: " << (matches ? "YES" : "NO") << "\n";
    
    // Also test with the legacy version for comparison
    std::cout << "\n=== Testing legacy encryption with key 0xDEADBEEF ===\n";
    entry_t legacy = original;
    ret = ecall_encrypt_entry(global_eid, &crypto_ret, &legacy, 0xDEADBEEF);
    if (ret == SGX_SUCCESS && crypto_ret == CRYPTO_SUCCESS) {
        print_entry(legacy, "Legacy encrypted");
        
        ret = ecall_decrypt_entry(global_eid, &crypto_ret, &legacy, 0xDEADBEEF);
        if (ret == SGX_SUCCESS && crypto_ret == CRYPTO_SUCCESS) {
            print_entry(legacy, "Legacy decrypted");
            
            if (legacy.join_attr == original.join_attr) {
                std::cout << "Legacy encryption/decryption works correctly\n";
            } else {
                std::cout << "Legacy encryption/decryption FAILED\n";
            }
        }
    }
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    
    return matches ? 0 : 1;
}