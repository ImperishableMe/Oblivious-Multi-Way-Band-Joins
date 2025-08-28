#include <iostream>
#include <cstring>
#include "app/data_structures/entry.h"
#include "app/crypto/crypto_utils.h"
#include "app/Enclave_u.h"
#include "sgx_urts.h"

sgx_enclave_id_t global_eid = 0;

// Forward declarations
void test_simple_conversion();
void test_with_ecall();
void test_encrypted();
void test_proper_encryption();
void test_load_encrypted_csv();

bool init_enclave() {
    sgx_status_t ret = SGX_SUCCESS;
    sgx_launch_token_t token = {0};
    int updated = 0;
    
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return false;
    }
    return true;
}

void test_simple_conversion() {
    std::cout << "\n=== Test 1: Simple conversion without enclave ===" << std::endl;
    
    // Create a simple Entry with known values
    Entry original;
    original.field_type = 1;
    original.equality_type = 2;
    original.is_encrypted = false;  // NOT encrypted
    original.nonce = 0;
    original.join_attr = 100;
    original.original_index = 200;
    original.local_mult = 300;
    original.attributes.push_back(1000);
    original.attributes.push_back(2000);
    original.column_names.push_back("col1");
    original.column_names.push_back("col2");
    
    std::cout << "Original Entry:" << std::endl;
    std::cout << "  field_type: " << original.field_type << std::endl;
    std::cout << "  join_attr: " << original.join_attr << std::endl;
    std::cout << "  original_index: " << original.original_index << std::endl;
    std::cout << "  local_mult: " << original.local_mult << std::endl;
    
    // Convert to entry_t
    entry_t c_entry = original.to_entry_t();
    
    std::cout << "\nAfter to_entry_t():" << std::endl;
    std::cout << "  field_type: " << c_entry.field_type << std::endl;
    std::cout << "  join_attr: " << c_entry.join_attr << std::endl;
    std::cout << "  original_index: " << c_entry.original_index << std::endl;
    std::cout << "  local_mult: " << c_entry.local_mult << std::endl;
    
    // Convert back to Entry
    Entry recovered;
    recovered.from_entry_t(c_entry);
    
    std::cout << "\nAfter from_entry_t():" << std::endl;
    std::cout << "  field_type: " << recovered.field_type << std::endl;
    std::cout << "  join_attr: " << recovered.join_attr << std::endl;
    std::cout << "  original_index: " << recovered.original_index << std::endl;
    std::cout << "  local_mult: " << recovered.local_mult << std::endl;
    
    // Check if values match
    bool success = (recovered.field_type == original.field_type &&
                   recovered.join_attr == original.join_attr &&
                   recovered.original_index == original.original_index &&
                   recovered.local_mult == original.local_mult);
    
    std::cout << "\nResult: " << (success ? "SUCCESS" : "FAILED") << std::endl;
}

void test_with_ecall() {
    std::cout << "\n=== Test 2: With enclave (no-op transform) ===" << std::endl;
    
    // Create Entry with known values
    Entry original;
    original.field_type = 1;
    original.equality_type = 2;
    original.is_encrypted = false;  // NOT encrypted
    original.join_attr = 100;
    original.original_index = 200;
    original.local_mult = 300;
    
    std::cout << "Original Entry:" << std::endl;
    std::cout << "  field_type: " << original.field_type << std::endl;
    std::cout << "  join_attr: " << original.join_attr << std::endl;
    std::cout << "  original_index: " << original.original_index << std::endl;
    
    // Convert to entry_t
    entry_t c_entry = original.to_entry_t();
    
    // Call a simple enclave function (set_index which should only change original_index)
    sgx_status_t status = ecall_transform_set_index(global_eid, &c_entry, 999);
    if (status != SGX_SUCCESS) {
        std::cerr << "Ecall failed with status: " << status << std::endl;
        return;
    }
    
    std::cout << "\nAfter ecall (entry_t):" << std::endl;
    std::cout << "  field_type: " << c_entry.field_type << std::endl;
    std::cout << "  join_attr: " << c_entry.join_attr << std::endl;
    std::cout << "  original_index: " << c_entry.original_index << " (should be 999)" << std::endl;
    
    // Convert back to Entry
    Entry recovered;
    recovered.from_entry_t(c_entry);
    
    std::cout << "\nAfter from_entry_t():" << std::endl;
    std::cout << "  field_type: " << recovered.field_type << std::endl;
    std::cout << "  join_attr: " << recovered.join_attr << std::endl;
    std::cout << "  original_index: " << recovered.original_index << std::endl;
    
    // Check if values are preserved (except original_index which should be 999)
    bool success = (recovered.field_type == original.field_type &&
                   recovered.join_attr == original.join_attr &&
                   recovered.original_index == 999);
    
    std::cout << "\nResult: " << (success ? "SUCCESS" : "FAILED") << std::endl;
}

void test_encrypted() {
    std::cout << "\n=== Test 3: With encryption ===" << std::endl;
    
    // Create Entry with encryption flag BUT not actually encrypted yet
    Entry original;
    original.field_type = 1;
    original.equality_type = 2;
    original.is_encrypted = true;  // Mark as encrypted (but values are still plaintext)
    original.nonce = 12345;
    original.join_attr = 100;
    original.original_index = 200;
    original.local_mult = 300;
    
    std::cout << "Original Entry (marked as encrypted but plaintext values):" << std::endl;
    std::cout << "  field_type: " << original.field_type << std::endl;
    std::cout << "  is_encrypted: " << original.is_encrypted << std::endl;
    std::cout << "  join_attr: " << original.join_attr << std::endl;
    
    // Convert to entry_t
    entry_t c_entry = original.to_entry_t();
    
    std::cout << "\nAfter to_entry_t() (should still be plaintext):" << std::endl;
    std::cout << "  field_type: " << c_entry.field_type << std::endl;
    std::cout << "  is_encrypted: " << (int)c_entry.is_encrypted << std::endl;
    std::cout << "  join_attr: " << c_entry.join_attr << std::endl;
    
    // Call enclave function - the enclave will try to decrypt since is_encrypted=true
    sgx_status_t status = ecall_transform_set_index(global_eid, &c_entry, 999);
    if (status != SGX_SUCCESS) {
        std::cerr << "Ecall failed with status: " << status << std::endl;
        return;
    }
    
    std::cout << "\nAfter ecall (enclave decrypted then re-encrypted):" << std::endl;
    std::cout << "  field_type: " << c_entry.field_type << std::endl;
    std::cout << "  is_encrypted: " << (int)c_entry.is_encrypted << std::endl;
    std::cout << "  join_attr: " << c_entry.join_attr << std::endl;
    
    // Convert back to Entry
    Entry recovered;
    recovered.from_entry_t(c_entry);
    
    std::cout << "\nAfter from_entry_t() (still encrypted values):" << std::endl;
    std::cout << "  field_type: " << recovered.field_type << " (encrypted, should NOT be 1)" << std::endl;
    std::cout << "  is_encrypted: " << recovered.is_encrypted << std::endl;
    std::cout << "  join_attr: " << recovered.join_attr << " (encrypted, should NOT be 100)" << std::endl;
    std::cout << "  original_index: " << recovered.original_index << " (encrypted, should NOT be 999)" << std::endl;
    
    // The issue: We marked it as encrypted but didn't actually encrypt the values
    // The enclave tried to decrypt plaintext values, getting garbage
    // Then it re-encrypted the garbage
    std::cout << "\nProblem: We set is_encrypted=true but didn't actually encrypt the values!" << std::endl;
    std::cout << "The enclave tries to decrypt plaintext, gets garbage, then re-encrypts garbage." << std::endl;
}

void test_proper_encryption() {
    std::cout << "\n=== Test 4: Proper encryption flow ===" << std::endl;
    
    // Create Entry with plaintext values, NOT encrypted
    Entry original;
    original.field_type = 1;
    original.equality_type = 2;
    original.is_encrypted = false;  // Start with plaintext
    original.nonce = 0;
    original.join_attr = 100;
    original.original_index = 200;
    original.local_mult = 300;
    original.attributes.push_back(1000);
    original.attributes.push_back(2000);
    
    std::cout << "Original Entry (plaintext):" << std::endl;
    std::cout << "  field_type: " << original.field_type << std::endl;
    std::cout << "  is_encrypted: " << original.is_encrypted << std::endl;
    std::cout << "  join_attr: " << original.join_attr << std::endl;
    std::cout << "  attributes: " << original.attributes[0] << ", " << original.attributes[1] << std::endl;
    
    // Properly encrypt the entry using CryptoUtils
    crypto_status_t cstatus = CryptoUtils::encrypt_entry(original, global_eid);
    if (cstatus != CRYPTO_SUCCESS) {
        std::cerr << "Encryption failed with status: " << cstatus << std::endl;
        return;
    }
    
    std::cout << "\nAfter encrypt_entry() (now properly encrypted):" << std::endl;
    std::cout << "  field_type: " << original.field_type << " (encrypted)" << std::endl;
    std::cout << "  is_encrypted: " << original.is_encrypted << std::endl;
    std::cout << "  join_attr: " << original.join_attr << " (encrypted)" << std::endl;
    
    // Convert to entry_t
    entry_t c_entry = original.to_entry_t();
    
    // Call enclave function - now it can properly decrypt
    sgx_status_t status = ecall_transform_set_index(global_eid, &c_entry, 999);
    if (status != SGX_SUCCESS) {
        std::cerr << "Ecall failed with status: " << status << std::endl;
        return;
    }
    
    // Convert back to Entry
    Entry transformed;
    transformed.from_entry_t(c_entry);
    
    std::cout << "\nAfter transform (still encrypted):" << std::endl;
    std::cout << "  field_type: " << transformed.field_type << " (encrypted)" << std::endl;
    std::cout << "  is_encrypted: " << transformed.is_encrypted << std::endl;
    std::cout << "  join_attr: " << transformed.join_attr << " (encrypted)" << std::endl;
    
    // Decrypt to see the actual values
    cstatus = CryptoUtils::decrypt_entry(transformed, global_eid);
    if (cstatus != CRYPTO_SUCCESS) {
        std::cerr << "Decryption failed with status: " << cstatus << std::endl;
        return;
    }
    
    std::cout << "\nAfter decrypt_entry() (plaintext):" << std::endl;
    std::cout << "  field_type: " << transformed.field_type << " (should be 1)" << std::endl;
    std::cout << "  is_encrypted: " << transformed.is_encrypted << std::endl;
    std::cout << "  join_attr: " << transformed.join_attr << " (should be 100)" << std::endl;
    std::cout << "  original_index: " << transformed.original_index << " (should be 999)" << std::endl;
    if (transformed.attributes.size() >= 2) {
        std::cout << "  attributes: " << transformed.attributes[0] << ", " << transformed.attributes[1] 
                  << " (should be 1000, 2000)" << std::endl;
    } else {
        std::cout << "  attributes: size=" << transformed.attributes.size() << " (expected 2)" << std::endl;
    }
    
    // Check if values are correct
    bool success = (transformed.field_type == 1 &&
                   transformed.join_attr == 100 &&
                   transformed.original_index == 999 &&
                   transformed.attributes.size() == 2 &&
                   transformed.attributes[0] == 1000 &&
                   transformed.attributes[1] == 2000);
    
    std::cout << "\nResult: " << (success ? "SUCCESS" : "FAILED") << std::endl;
}

void test_load_encrypted_csv() {
    std::cout << "\n=== Test 5: Simulate loading encrypted CSV ===" << std::endl;
    std::cout << "This simulates what happens when we load an encrypted CSV file." << std::endl;
    std::cout << "The CSV contains ciphertext values, and is_encrypted=true." << std::endl;
    
    // Step 1: Start with plaintext entry
    Entry plaintext;
    plaintext.field_type = 1;
    plaintext.equality_type = 2;
    plaintext.is_encrypted = false;
    plaintext.nonce = 0;
    plaintext.join_attr = 100;
    plaintext.original_index = 200;
    plaintext.local_mult = 300;
    plaintext.attributes.push_back(1000);
    plaintext.attributes.push_back(2000);
    
    std::cout << "\n1. Original plaintext entry:" << std::endl;
    std::cout << "   field_type=" << plaintext.field_type 
              << ", join_attr=" << plaintext.join_attr 
              << ", attrs=[" << plaintext.attributes[0] << "," << plaintext.attributes[1] << "]" << std::endl;
    
    // Step 2: Encrypt it (simulating what encrypt_tables does)
    crypto_status_t status = CryptoUtils::encrypt_entry(plaintext, global_eid);
    if (status != CRYPTO_SUCCESS) {
        std::cerr << "Encryption failed" << std::endl;
        return;
    }
    
    std::cout << "\n2. After encryption (this is what gets saved to CSV):" << std::endl;
    std::cout << "   field_type=" << plaintext.field_type << " (ciphertext)"
              << ", join_attr=" << plaintext.join_attr << " (ciphertext)"
              << ", is_encrypted=" << plaintext.is_encrypted << std::endl;
    
    // Step 3: Simulate loading from CSV
    // The CSV loader would create an Entry with the ciphertext values and is_encrypted=true
    Entry loaded;
    loaded.field_type = plaintext.field_type;  // Ciphertext value from CSV
    loaded.equality_type = plaintext.equality_type;  // Ciphertext value from CSV
    loaded.is_encrypted = true;  // Set because nonce column exists
    loaded.nonce = plaintext.nonce;  // From CSV
    loaded.join_attr = plaintext.join_attr;  // Ciphertext value from CSV
    loaded.original_index = plaintext.original_index;  // Ciphertext value from CSV
    loaded.local_mult = plaintext.local_mult;  // Ciphertext value from CSV
    loaded.attributes = plaintext.attributes;  // Ciphertext values from CSV
    
    std::cout << "\n3. Loaded from CSV (ciphertext values, is_encrypted=true):" << std::endl;
    std::cout << "   This Entry now contains ciphertext and is marked as encrypted." << std::endl;
    std::cout << "   field_type=" << loaded.field_type << " (ciphertext)"
              << ", join_attr=" << loaded.join_attr << " (ciphertext)" << std::endl;
    
    // Step 4: Use it in an ecall - the enclave should be able to decrypt and process it
    entry_t c_entry = loaded.to_entry_t();
    
    sgx_status_t sgx_status = ecall_transform_set_index(global_eid, &c_entry, 999);
    if (sgx_status != SGX_SUCCESS) {
        std::cerr << "Ecall failed" << std::endl;
        return;
    }
    
    Entry after_ecall;
    after_ecall.from_entry_t(c_entry);
    
    std::cout << "\n4. After ecall (should still be encrypted):" << std::endl;
    std::cout << "   field_type=" << after_ecall.field_type << " (ciphertext)"
              << ", original_index=" << after_ecall.original_index << " (ciphertext, should encode 999)" << std::endl;
    
    // Step 5: Decrypt to verify
    status = CryptoUtils::decrypt_entry(after_ecall, global_eid);
    if (status != CRYPTO_SUCCESS) {
        std::cerr << "Decryption failed" << std::endl;
        return;
    }
    
    std::cout << "\n5. After decryption (should show correct values):" << std::endl;
    std::cout << "   field_type=" << after_ecall.field_type << " (should be 1)"
              << ", join_attr=" << after_ecall.join_attr << " (should be 100)"
              << ", original_index=" << after_ecall.original_index << " (should be 999)" << std::endl;
    
    if (after_ecall.attributes.size() >= 2) {
        std::cout << "   attributes=[" << after_ecall.attributes[0] << "," << after_ecall.attributes[1] 
                  << "] (should be [1000,2000])" << std::endl;
    }
    
    // Check success
    bool success = (after_ecall.field_type == 1 &&
                   after_ecall.join_attr == 100 &&
                   after_ecall.original_index == 999);
    
    std::cout << "\nResult: " << (success ? "SUCCESS" : "FAILED") << std::endl;
}

int main() {
    // Test 1: Just the conversion without enclave
    test_simple_conversion();
    
    // Initialize enclave for remaining tests
    if (!init_enclave()) {
        return 1;
    }
    std::cout << "Enclave initialized" << std::endl;
    
    // Test 2: With enclave but no encryption
    test_with_ecall();
    
    // Test 3: With enclave and encryption (but incorrectly marked)
    test_encrypted();
    
    // Test 4: Properly encrypt then transform
    test_proper_encryption();
    
    // Test 5: Simulate loading encrypted CSV (ciphertext values, is_encrypted=true)
    test_load_encrypted_csv();
    
    sgx_destroy_enclave(global_eid);
    return 0;
}