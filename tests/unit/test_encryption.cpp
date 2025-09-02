#include <iostream>
#include <cstring>
#include <cassert>
#include "sgx_urts.h"
#include "app/Enclave_u.h"
#include "enclave/enclave_types.h"
#include "enclave/crypto/entry_crypto.h"

// External test reporting function
extern void report_test_result(const std::string& test_name, bool passed);

// Helper function to create a test entry
entry_t create_test_entry() {
    entry_t entry;
    memset(&entry, 0, sizeof(entry_t));
    
    entry.field_type = SOURCE;
    entry.equality_type = EQ;
    entry.is_encrypted = 0;
    entry.join_attr = 42;
    entry.original_index = 100;
    entry.local_mult = 10;
    entry.final_mult = 20;
    entry.foreign_sum = 30;
    entry.local_cumsum = 40;
    entry.local_interval = 50;
    // foreign_cumsum removed
    entry.foreign_interval = 70;
    entry.local_weight = 80;
    entry.copy_index = 90;
    entry.alignment_key = 95;
    
    // Set some attributes
    for (int i = 0; i < 5; i++) {
        entry.attributes[i] = i * 1.5;
    }
    
    // Set some column names
    strcpy(entry.column_names[0], "col1");
    strcpy(entry.column_names[1], "col2");
    
    return entry;
}

// Test basic encryption and decryption
void test_basic_encryption_decryption(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t entry = create_test_entry();
    entry_t original = entry;  // Save original for comparison
    int32_t key = 0xDEADBEEF;
    crypto_status_t status;
    
    // Test encryption
    sgx_status_t ret = ecall_encrypt_entry(eid, &status, &entry, key);
    if (ret != SGX_SUCCESS || status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed with SGX status: " << ret 
                  << ", crypto status: " << status << std::endl;
    }
    
    // Verify encrypted
    if (passed) {
        if (entry.is_encrypted != 1) {
            passed = false;
            std::cerr << "Entry not marked as encrypted" << std::endl;
        }
        if (entry.join_attr == original.join_attr) {
            passed = false;
            std::cerr << "join_attr not encrypted" << std::endl;
        }
        if (entry.original_index == original.original_index) {
            passed = false;
            std::cerr << "original_index not encrypted" << std::endl;
        }
    }
    
    // Test decryption
    if (passed) {
        ret = ecall_decrypt_entry(eid, &status, &entry, key);
        if (ret != SGX_SUCCESS || status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed" << std::endl;
        }
    }
    
    // Verify decrypted correctly
    if (passed) {
        if (entry.is_encrypted != 0) {
            passed = false;
            std::cerr << "Entry still marked as encrypted" << std::endl;
        }
        if (entry.join_attr != original.join_attr) {
            passed = false;
            std::cerr << "join_attr not restored: " << entry.join_attr 
                      << " vs " << original.join_attr << std::endl;
        }
        if (entry.original_index != original.original_index) {
            passed = false;
            std::cerr << "original_index not restored" << std::endl;
        }
        if (entry.local_mult != original.local_mult) {
            passed = false;
            std::cerr << "local_mult not restored" << std::endl;
        }
    }
    
    report_test_result("Basic Encryption/Decryption", passed);
}

// Test double encryption prevention
void test_double_encryption_prevention(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t entry = create_test_entry();
    int32_t key = 0xDEADBEEF;
    crypto_status_t status;
    
    // First encryption
    sgx_status_t ret = ecall_encrypt_entry(eid, &status, &entry, key);
    if (ret != SGX_SUCCESS || status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "First encryption failed" << std::endl;
    }
    
    // Try to encrypt again - should fail
    if (passed) {
        ret = ecall_encrypt_entry(eid, &status, &entry, key);
        if (ret != SGX_SUCCESS) {
            passed = false;
            std::cerr << "SGX call failed on double encryption" << std::endl;
        } else if (status != CRYPTO_ALREADY_ENCRYPTED) {
            passed = false;
            std::cerr << "Double encryption not prevented. Status: " << status << std::endl;
        }
    }
    
    report_test_result("Double Encryption Prevention", passed);
}

// Test double decryption prevention
void test_double_decryption_prevention(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t entry = create_test_entry();
    int32_t key = 0xDEADBEEF;
    crypto_status_t status;
    
    // Try to decrypt unencrypted entry - should fail
    sgx_status_t ret = ecall_decrypt_entry(eid, &status, &entry, key);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    } else if (status != CRYPTO_NOT_ENCRYPTED) {
        passed = false;
        std::cerr << "Decrypting unencrypted entry not prevented. Status: " << status << std::endl;
    }
    
    report_test_result("Double Decryption Prevention", passed);
}

// Test that column names are not encrypted
void test_column_names_not_encrypted(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t entry = create_test_entry();
    strcpy(entry.column_names[0], "test_col");
    char original_name[MAX_COLUMN_NAME_LEN];
    strcpy(original_name, entry.column_names[0]);
    
    int32_t key = 0xCAFEBABE;
    crypto_status_t status;
    
    // Encrypt
    sgx_status_t ret = ecall_encrypt_entry(eid, &status, &entry, key);
    if (ret != SGX_SUCCESS || status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed" << std::endl;
    }
    
    // Check column name unchanged
    if (passed && strcmp(entry.column_names[0], original_name) != 0) {
        passed = false;
        std::cerr << "Column name was encrypted when it shouldn't be" << std::endl;
    }
    
    report_test_result("Column Names Not Encrypted", passed);
}

// Test batch encryption
void test_batch_encryption(sgx_enclave_id_t eid) {
    bool passed = true;
    const size_t count = 5;
    entry_t entries[count];
    
    // Create test entries
    for (size_t i = 0; i < count; i++) {
        entries[i] = create_test_entry();
        entries[i].original_index = i * 10;
    }
    
    int32_t key = 0x12345678;
    crypto_status_t status;
    
    // Batch encrypt
    sgx_status_t ret = ecall_encrypt_entries(eid, &status, entries, count, key);
    if (ret != SGX_SUCCESS || status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Batch encryption failed" << std::endl;
    }
    
    // Verify all encrypted
    if (passed) {
        for (size_t i = 0; i < count; i++) {
            if (entries[i].is_encrypted != 1) {
                passed = false;
                std::cerr << "Entry " << i << " not encrypted" << std::endl;
                break;
            }
        }
    }
    
    // Batch decrypt
    if (passed) {
        ret = ecall_decrypt_entries(eid, &status, entries, count, key);
        if (ret != SGX_SUCCESS || status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Batch decryption failed" << std::endl;
        }
    }
    
    // Verify all decrypted correctly
    if (passed) {
        for (size_t i = 0; i < count; i++) {
            if (entries[i].is_encrypted != 0) {
                passed = false;
                std::cerr << "Entry " << i << " still encrypted" << std::endl;
                break;
            }
            if (entries[i].original_index != i * 10) {
                passed = false;
                std::cerr << "Entry " << i << " data corrupted" << std::endl;
                break;
            }
        }
    }
    
    report_test_result("Batch Encryption/Decryption", passed);
}

// Main encryption test suite
void run_encryption_tests(sgx_enclave_id_t eid) {
    test_basic_encryption_decryption(eid);
    test_double_encryption_prevention(eid);
    test_double_decryption_prevention(eid);
    test_column_names_not_encrypted(eid);
    test_batch_encryption(eid);
}