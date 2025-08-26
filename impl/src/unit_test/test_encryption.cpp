#include "../app/crypto_utils.h"
#include "../app/converters.h"
#include "../enclave/crypto/entry_crypto.h"
#include "Enclave_u.h"
#include "sgx_urts.h"
#include <cassert>
#include <iostream>
#include <cstring>
#include <cstddef>

/**
 * Test that double encryption is prevented
 */
void test_double_encryption_prevention(sgx_enclave_id_t eid) {
    std::cout << "Testing double encryption prevention..." << std::endl;
    
    Entry entry;
    entry.original_index = 42;
    entry.local_mult = 10;
    entry.join_attr = 3.14159;
    entry.is_encrypted = false;
    entry.field_type = SOURCE;
    entry.equality_type = EQ;
    
    // Add some attributes and column names
    for (int i = 0; i < 5; i++) {
        entry.attributes.push_back(i * 1.5);
        entry.column_names.push_back("col" + std::to_string(i));
    }
    
    uint32_t key = 0xDEADBEEF;
    
    // First encryption should succeed
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, key, eid);
    assert(status == CRYPTO_SUCCESS);
    assert(entry.is_encrypted == true);
    assert(entry.original_index != 42);  // Should be encrypted
    assert(entry.field_type != SOURCE);  // Enums should be encrypted too
    
    // Second encryption should fail with warning
    status = CryptoUtils::encrypt_entry(entry, key, eid);
    assert(status == CRYPTO_ALREADY_ENCRYPTED);
    assert(entry.is_encrypted == true);  // Still encrypted
    
    // Decryption should succeed
    status = CryptoUtils::decrypt_entry(entry, key, eid);
    assert(status == CRYPTO_SUCCESS);
    assert(entry.is_encrypted == false);
    assert(entry.original_index == 42);  // Back to original
    assert(entry.field_type == SOURCE);  // Enums back to original
    
    // Double decryption should fail with warning
    status = CryptoUtils::decrypt_entry(entry, key, eid);
    assert(status == CRYPTO_NOT_ENCRYPTED);
    assert(entry.is_encrypted == false);
    
    std::cout << "✓ Double encryption prevention test passed" << std::endl;
}

/**
 * Test that XOR encryption is reversible
 */
void test_encryption_roundtrip(sgx_enclave_id_t eid) {
    std::cout << "Testing encryption roundtrip..." << std::endl;
    
    entry_t original, encrypted, decrypted;
    
    // Initialize with test data
    memset(&original, 0, sizeof(entry_t));
    original.field_type = SOURCE;
    original.equality_type = EQ;
    original.is_encrypted = false;
    original.join_attr = 3.14159;
    original.original_index = 42;
    original.local_mult = 7;
    original.final_mult = 21;
    original.foreign_sum = 100;
    
    // Fill attributes with test values
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        original.attributes[i] = i * 1.5;
    }
    
    // Copy for encryption
    encrypted = original;
    
    // Encrypt
    uint32_t key = 0xDEADBEEF;
    crypto_status_t status = encrypt_entry(&encrypted, key);
    assert(status == CRYPTO_SUCCESS);
    assert(encrypted.is_encrypted == true);
    assert(encrypted.original_index != 42);  // Should be different
    assert(encrypted.local_mult != 7);
    assert(encrypted.join_attr != 3.14159);
    
    // Field_type and equality_type SHOULD be encrypted now
    assert(encrypted.field_type != SOURCE);
    assert(encrypted.equality_type != EQ);
    
    // Decrypt
    decrypted = encrypted;
    status = decrypt_entry(&decrypted, key);
    assert(status == CRYPTO_SUCCESS);
    assert(decrypted.is_encrypted == false);
    assert(decrypted.original_index == 42);  // Should match original
    assert(decrypted.local_mult == 7);
    assert(decrypted.final_mult == 21);
    assert(decrypted.join_attr == 3.14159);
    assert(decrypted.field_type == SOURCE);  // Should be restored
    assert(decrypted.equality_type == EQ);  // Should be restored
    
    // Check all attributes match
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        assert(decrypted.attributes[i] == original.attributes[i]);
    }
    
    std::cout << "✓ Encryption roundtrip test passed" << std::endl;
}

/**
 * Test that all expected fields are encrypted
 * Now ALL fields should be encrypted except is_encrypted flag and column_names array
 */
void test_encryption_coverage(sgx_enclave_id_t eid) {
    std::cout << "Testing encryption coverage..." << std::endl;
    
    Entry entry1, entry2;
    
    // Fill both with same pattern
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        entry1.attributes.push_back(0xAAAAAAAA);
        entry2.attributes.push_back(0xAAAAAAAA);
        entry1.column_names.push_back("TESTCOL");
        entry2.column_names.push_back("TESTCOL");
    }
    
    // Set all fields to same values
    entry1.field_type = entry2.field_type = SOURCE;
    entry1.equality_type = entry2.equality_type = EQ;
    entry1.is_encrypted = entry2.is_encrypted = false;
    entry1.original_index = entry2.original_index = 0xAAAAAAAA;
    entry1.local_mult = entry2.local_mult = 0xAAAAAAAA;
    entry1.final_mult = entry2.final_mult = 0xAAAAAAAA;
    entry1.join_attr = entry2.join_attr = *(double*)"\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA";
    
    // Encrypt one through SGX
    uint32_t key = 0xDEADBEEF;
    crypto_status_t status = CryptoUtils::encrypt_entry(entry1, key, eid);
    assert(status == CRYPTO_SUCCESS);
    assert(entry1.is_encrypted == true);
    
    // Convert to entry_t for byte comparison
    entry_t c_entry1 = entry_to_entry_t(entry1);
    entry_t c_entry2 = entry_to_entry_t(entry2);
    
    // Count unchanged bytes
    size_t unchanged_bytes = 0;
    uint8_t* p1 = (uint8_t*)&c_entry1;
    uint8_t* p2 = (uint8_t*)&c_entry2;
    
    for (size_t i = 0; i < sizeof(entry_t); i++) {
        if (p1[i] == p2[i]) {
            unchanged_bytes++;
        }
    }
    
    // Calculate expected unchanged bytes:
    // - is_encrypted flag: 1 byte
    // - column_names array: MAX_ATTRIBUTES * MAX_COLUMN_NAME_LEN bytes
    size_t is_encrypted_size = sizeof(bool);
    size_t column_names_size = MAX_ATTRIBUTES * MAX_COLUMN_NAME_LEN;
    size_t expected_unchanged = is_encrypted_size + column_names_size;
    
    // Allow some padding tolerance
    size_t padding_tolerance = 32;
    size_t max_allowed_unchanged = expected_unchanged + padding_tolerance;
    
    if (unchanged_bytes > max_allowed_unchanged) {
        std::cerr << "ERROR: Too many unchanged bytes!" << std::endl;
        std::cerr << "Unchanged: " << unchanged_bytes << " bytes" << std::endl;
        std::cerr << "Expected: " << expected_unchanged << " bytes (is_encrypted + column_names)" << std::endl;
        std::cerr << "Max allowed with padding: " << max_allowed_unchanged << " bytes" << std::endl;
        std::cerr << "This likely means some fields are not being encrypted!" << std::endl;
        assert(false);
    }
    
    std::cout << "✓ Encryption coverage test passed (" << unchanged_bytes 
              << " bytes unchanged out of expected " << expected_unchanged << ")" << std::endl;
}

/**
 * Test detailed field encryption coverage
 * With the new approach, only is_encrypted and column_names should be unchanged
 */
void test_field_coverage_detailed(sgx_enclave_id_t eid) {
    std::cout << "\n=== Detailed Field Encryption Coverage Test ===" << std::endl;
    std::cout << "sizeof(entry_t) = " << sizeof(entry_t) << " bytes" << std::endl;
    
    Entry entry1, entry2;
    
    // Fill both with same pattern (0xAA = 10101010 in binary)
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        double val = 0.0;
        memset(&val, 0xAA, sizeof(double));
        entry1.attributes.push_back(val);
        entry2.attributes.push_back(val);
        
        std::string colname(MAX_COLUMN_NAME_LEN - 1, 0xAA);
        entry1.column_names.push_back(colname);
        entry2.column_names.push_back(colname);
    }
    
    // Set all fields to same pattern
    entry1.field_type = entry2.field_type = (entry_type_t)0xAAAAAAAA;
    entry1.equality_type = entry2.equality_type = (equality_type_t)0xAAAAAAAA;
    entry1.is_encrypted = entry2.is_encrypted = false;
    entry1.original_index = entry2.original_index = 0xAAAAAAAA;
    entry1.local_mult = entry2.local_mult = 0xAAAAAAAA;
    entry1.final_mult = entry2.final_mult = 0xAAAAAAAA;
    entry1.foreign_sum = entry2.foreign_sum = 0xAAAAAAAA;
    entry1.local_cumsum = entry2.local_cumsum = 0xAAAAAAAA;
    entry1.local_interval = entry2.local_interval = 0xAAAAAAAA;
    entry1.foreign_cumsum = entry2.foreign_cumsum = 0xAAAAAAAA;
    entry1.foreign_interval = entry2.foreign_interval = 0xAAAAAAAA;
    entry1.local_weight = entry2.local_weight = 0xAAAAAAAA;
    entry1.copy_index = entry2.copy_index = 0xAAAAAAAA;
    entry1.alignment_key = entry2.alignment_key = 0xAAAAAAAA;
    
    double pattern_double;
    memset(&pattern_double, 0xAA, sizeof(double));
    entry1.join_attr = entry2.join_attr = pattern_double;
    
    // Encrypt one through SGX
    uint32_t key = 0xDEADBEEF;
    crypto_status_t status = CryptoUtils::encrypt_entry(entry1, key, eid);
    assert(status == CRYPTO_SUCCESS);
    assert(entry1.is_encrypted == true);
    
    // Convert to entry_t for byte comparison
    entry_t c_entry1 = entry_to_entry_t(entry1);
    entry_t c_entry2 = entry_to_entry_t(entry2);
    
    // Compare byte by byte
    uint8_t* p1 = (uint8_t*)&c_entry1;
    uint8_t* p2 = (uint8_t*)&c_entry2;
    
    size_t unchanged_bytes = 0;
    bool found_unexpected_unchanged = false;
    
    std::cout << "\nChecking which bytes remain unchanged:" << std::endl;
    
    // Calculate expected unchanged regions
    size_t is_encrypted_offset = offsetof(entry_t, is_encrypted);
    size_t is_encrypted_end = is_encrypted_offset + sizeof(bool);
    size_t column_names_offset = offsetof(entry_t, column_names);
    size_t column_names_end = column_names_offset + (MAX_ATTRIBUTES * MAX_COLUMN_NAME_LEN);
    
    std::cout << "Expected unchanged regions:" << std::endl;
    std::cout << "  - is_encrypted: offset " << is_encrypted_offset << "-" << is_encrypted_end << std::endl;
    std::cout << "  - column_names: offset " << column_names_offset << "-" << column_names_end << std::endl;
    
    for (size_t i = 0; i < sizeof(entry_t); i++) {
        bool is_excluded = false;
        
        // Check if byte is in is_encrypted field
        if (i >= is_encrypted_offset && i < is_encrypted_end) {
            is_excluded = true;
        }
        
        // Check if byte is in column_names array
        if (i >= column_names_offset && i < column_names_end) {
            is_excluded = true;
        }
        
        bool is_unchanged = (p1[i] == p2[i]);
        
        if (is_unchanged) {
            unchanged_bytes++;
            
            // Check if this byte SHOULD have been encrypted
            if (!is_excluded) {
                if (!found_unexpected_unchanged) {
                    std::cout << "\nWARNING: Found unchanged bytes that should be encrypted:" << std::endl;
                    found_unexpected_unchanged = true;
                }
                
                // Try to identify which field this belongs to
                std::cout << "  Byte " << i << " (0x" << std::hex << (int)p1[i] << std::dec << ")";
                
                // Identify the field
                if (i >= offsetof(entry_t, field_type) && 
                    i < offsetof(entry_t, field_type) + sizeof(entry_type_t)) {
                    std::cout << " - in field_type field!";
                } else if (i >= offsetof(entry_t, equality_type) && 
                          i < offsetof(entry_t, equality_type) + sizeof(equality_type_t)) {
                    std::cout << " - in equality_type field!";
                } else if (i >= offsetof(entry_t, original_index) && 
                          i < offsetof(entry_t, original_index) + sizeof(uint32_t)) {
                    std::cout << " - in original_index field!";
                } else if (i >= offsetof(entry_t, local_mult) && 
                          i < offsetof(entry_t, local_mult) + sizeof(uint32_t)) {
                    std::cout << " - in local_mult field!";
                } else if (i >= offsetof(entry_t, join_attr) && 
                          i < offsetof(entry_t, join_attr) + sizeof(double)) {
                    std::cout << " - in join_attr field!";
                } else if (i >= offsetof(entry_t, attributes) && 
                          i < offsetof(entry_t, attributes) + sizeof(double) * MAX_ATTRIBUTES) {
                    size_t attr_index = (i - offsetof(entry_t, attributes)) / sizeof(double);
                    std::cout << " - in attributes[" << attr_index << "] field!";
                } else {
                    std::cout << " - likely padding byte";
                }
                
                std::cout << std::endl;
            }
        }
    }
    
    // Summary
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Total bytes: " << sizeof(entry_t) << std::endl;
    std::cout << "Unchanged bytes: " << unchanged_bytes << std::endl;
    
    size_t expected_unchanged = sizeof(bool) + (MAX_ATTRIBUTES * MAX_COLUMN_NAME_LEN);
    std::cout << "Expected unchanged: " << expected_unchanged << " (is_encrypted + column_names)" << std::endl;
    
    // Allow some padding
    size_t padding_tolerance = 32;
    
    if (unchanged_bytes > expected_unchanged + padding_tolerance) {
        std::cerr << "\n✗ TEST FAILED: Too many unchanged bytes!" << std::endl;
        std::cerr << "Expected at most " << (expected_unchanged + padding_tolerance) << " unchanged bytes" << std::endl;
        std::cerr << "This likely means some fields are not being encrypted." << std::endl;
        assert(false);
    }
    
    if (found_unexpected_unchanged) {
        std::cerr << "\n✗ TEST FAILED: Found unexpected unchanged bytes outside excluded regions!" << std::endl;
        assert(false);
    }
    
    std::cout << "\n✓ Detailed field encryption coverage test passed!" << std::endl;
}

/**
 * Test batch encryption/decryption
 */
void test_batch_operations(sgx_enclave_id_t eid) {
    std::cout << "Testing batch encryption..." << std::endl;
    
    const size_t count = 5;
    entry_t entries[count];
    
    // Initialize entries with different values
    for (size_t i = 0; i < count; i++) {
        memset(&entries[i], 0, sizeof(entry_t));
        entries[i].original_index = i;
        entries[i].local_mult = i * 10;
        entries[i].join_attr = i * 3.14;
        entries[i].is_encrypted = false;
    }
    
    // Batch encrypt
    uint32_t key = 0xCAFEBABE;
    crypto_status_t status = encrypt_entries(entries, count, key);
    assert(status == CRYPTO_SUCCESS);
    
    // Check all are encrypted
    for (size_t i = 0; i < count; i++) {
        assert(entries[i].is_encrypted == true);
        assert(entries[i].original_index != i);  // Should be encrypted
    }
    
    // Try to encrypt again - should fail
    status = encrypt_entries(entries, count, key);
    assert(status == CRYPTO_ALREADY_ENCRYPTED);
    
    // Batch decrypt
    status = decrypt_entries(entries, count, key);
    assert(status == CRYPTO_SUCCESS);
    
    // Check all are decrypted correctly
    for (size_t i = 0; i < count; i++) {
        assert(entries[i].is_encrypted == false);
        assert(entries[i].original_index == i);  // Should be back to original
        assert(entries[i].local_mult == i * 10);
    }
    
    std::cout << "✓ Batch operations test passed" << std::endl;
}

/**
 * Initialize SGX enclave
 */
sgx_enclave_id_t initialize_enclave() {
    sgx_enclave_id_t eid = 0;
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    // Create the enclave
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        exit(1);
    }
    
    return eid;
}

/**
 * Main test runner
 */
int main() {
    std::cout << "\n=== Running SGX Encryption Tests ===" << std::endl;
    
    // Initialize enclave
    sgx_enclave_id_t eid = initialize_enclave();
    std::cout << "Enclave initialized with ID: " << eid << std::endl;
    
    // Run tests
    test_double_encryption_prevention(eid);
    test_encryption_roundtrip(eid);
    test_encryption_coverage(eid);
    test_field_coverage_detailed(eid);
    test_batch_operations(eid);
    
    // Destroy enclave
    sgx_destroy_enclave(eid);
    
    std::cout << "\n✓ All SGX encryption tests passed!" << std::endl;
    
    return 0;
}