#include <iostream>
#include <cstring>
#include <vector>
#include "../app/crypto/crypto_utils.h"
#include "../app/data_structures/entry.h"
#include "../app/Enclave_u.h"
#include "sgx_urts.h"

sgx_enclave_id_t global_eid = 0;

bool init_enclave() {
    sgx_status_t ret = SGX_SUCCESS;
    sgx_launch_token_t token = {0};
    int updated = 0;
    
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return false;
    }
    
    std::cout << "Enclave created successfully, EID: " << global_eid << std::endl;
    return true;
}

void test_simple_encryption() {
    std::cout << "\n=== Simple Encryption Test ===" << std::endl;
    
    // Create a simple entry
    Entry original;
    original.field_type = 1;
    original.equality_type = 2;
    original.is_encrypted = false;
    original.join_attr = 100;
    original.original_index = 5;
    original.local_mult = 10;
    
    // Add a few attributes
    original.attributes.push_back(1000);
    original.attributes.push_back(2000);
    original.attributes.push_back(3000);
    original.column_names.push_back("col1");
    original.column_names.push_back("col2");
    original.column_names.push_back("col3");
    
    std::cout << "Original values:" << std::endl;
    std::cout << "  join_attr: " << original.join_attr << std::endl;
    std::cout << "  local_mult: " << original.local_mult << std::endl;
    std::cout << "  attributes[0]: " << original.attributes[0] << std::endl;
    std::cout << "  attributes[1]: " << original.attributes[1] << std::endl;
    std::cout << "  attributes[2]: " << original.attributes[2] << std::endl;
    
    // Make a copy for encryption
    Entry encrypted = original;
    
    // Encrypt
    std::cout << "\nEncrypting..." << std::endl;
    crypto_status_t status = CryptoUtils::encrypt_entry(encrypted, global_eid);
    if (status != CRYPTO_SUCCESS) {
        std::cerr << "Encryption failed with status: " << status << std::endl;
        return;
    }
    
    std::cout << "After encryption:" << std::endl;
    std::cout << "  is_encrypted: " << (encrypted.is_encrypted ? "true" : "false") << std::endl;
    std::cout << "  join_attr: " << encrypted.join_attr << " (changed from " << original.join_attr << ")" << std::endl;
    std::cout << "  local_mult: " << encrypted.local_mult << " (changed from " << original.local_mult << ")" << std::endl;
    
    // Decrypt
    std::cout << "\nDecrypting..." << std::endl;
    status = CryptoUtils::decrypt_entry(encrypted, global_eid);
    if (status != CRYPTO_SUCCESS) {
        std::cerr << "Decryption failed with status: " << status << std::endl;
        return;
    }
    
    std::cout << "After decryption:" << std::endl;
    std::cout << "  is_encrypted: " << (encrypted.is_encrypted ? "true" : "false") << std::endl;
    std::cout << "  join_attr: " << encrypted.join_attr << std::endl;
    std::cout << "  local_mult: " << encrypted.local_mult << std::endl;
    std::cout << "  attributes[0]: " << encrypted.attributes[0] << std::endl;
    std::cout << "  attributes[1]: " << encrypted.attributes[1] << std::endl;
    std::cout << "  attributes[2]: " << encrypted.attributes[2] << std::endl;
    
    // Check if values match
    std::cout << "\n=== Verification ===" << std::endl;
    bool success = true;
    
    if (encrypted.join_attr != original.join_attr) {
        std::cerr << "FAILED: join_attr mismatch: " << original.join_attr << " != " << encrypted.join_attr << std::endl;
        success = false;
    }
    if (encrypted.local_mult != original.local_mult) {
        std::cerr << "FAILED: local_mult mismatch: " << original.local_mult << " != " << encrypted.local_mult << std::endl;
        success = false;
    }
    for (size_t i = 0; i < original.attributes.size(); i++) {
        if (encrypted.attributes[i] != original.attributes[i]) {
            std::cerr << "FAILED: attributes[" << i << "] mismatch: " << original.attributes[i] << " != " << encrypted.attributes[i] << std::endl;
            success = false;
        }
    }
    
    if (success) {
        std::cout << "SUCCESS: All values preserved correctly!" << std::endl;
    } else {
        std::cout << "FAILED: Some values were corrupted during encrypt/decrypt" << std::endl;
    }
}

void test_tpch_values() {
    std::cout << "\n=== TPCH Values Test ===" << std::endl;
    
    // Test with actual TPCH ACCTBAL values that are failing
    Entry entry;
    entry.is_encrypted = false;
    entry.attributes.push_back(575594);  // ACCTBAL value from TPCH
    entry.attributes.push_back(403268);  
    entry.attributes.push_back(121315);
    entry.column_names.push_back("ACCTBAL");
    entry.column_names.push_back("VALUE2");
    entry.column_names.push_back("VALUE3");
    
    std::cout << "Original TPCH values:" << std::endl;
    for (size_t i = 0; i < entry.attributes.size(); i++) {
        std::cout << "  " << entry.column_names[i] << ": " << entry.attributes[i] << std::endl;
    }
    
    Entry original = entry;
    
    // Encrypt and decrypt
    CryptoUtils::encrypt_entry(entry, global_eid);
    CryptoUtils::decrypt_entry(entry, global_eid);
    
    std::cout << "After encrypt/decrypt:" << std::endl;
    for (size_t i = 0; i < entry.attributes.size(); i++) {
        std::cout << "  " << entry.column_names[i] << ": " << entry.attributes[i];
        if (entry.attributes[i] != original.attributes[i]) {
            std::cout << " (CORRUPTED! was " << original.attributes[i] << ")";
        }
        std::cout << std::endl;
    }
}

int main() {
    std::cout << "=== Simple Encryption/Decryption Test ===" << std::endl;
    
    if (!init_enclave()) {
        return 1;
    }
    
    test_simple_encryption();
    test_tpch_values();
    
    // Destroy enclave
    sgx_destroy_enclave(global_eid);
    
    return 0;
}