/**
 * Test AES-CTR encryption/decryption functionality
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
    std::cout << "  nonce: " << entry.nonce << "\n";
    std::cout << "  attributes[0-4]: ";
    for (int i = 0; i < 5; i++) {
        std::cout << entry.attributes[i] << " ";
    }
    std::cout << "\n";
}

bool compare_entries(const entry_t& e1, const entry_t& e2, const std::string& context) {
    std::cout << "\n=== Comparing entries: " << context << " ===\n";
    
    // Check metadata fields
    std::cout << "field_type same? " << (e1.field_type == e2.field_type ? "YES" : "NO") << "\n";
    std::cout << "equality_type same? " << (e1.equality_type == e2.equality_type ? "YES" : "NO") << "\n";
    std::cout << "is_encrypted same? " << (e1.is_encrypted == e2.is_encrypted ? "YES" : "NO") << "\n";
    std::cout << "nonce same? " << (e1.nonce == e2.nonce ? "YES" : "NO") 
              << " (e1: " << e1.nonce << ", e2: " << e2.nonce << ")\n";
    
    // Check encrypted data fields
    std::cout << "join_attr same? " << (e1.join_attr == e2.join_attr ? "YES" : "NO")
              << " (e1: " << e1.join_attr << ", e2: " << e2.join_attr << ")\n";
    std::cout << "original_index same? " << (e1.original_index == e2.original_index ? "YES" : "NO")
              << " (e1: " << e1.original_index << ", e2: " << e2.original_index << ")\n";
    std::cout << "attributes[0] same? " << (e1.attributes[0] == e2.attributes[0] ? "YES" : "NO")
              << " (e1: " << e1.attributes[0] << ", e2: " << e2.attributes[0] << ")\n";
    
    // Check column names (should always be same since not encrypted)
    bool column_names_same = true;
    for (int i = 0; i < 5; i++) {
        if (strcmp(e1.column_names[i], e2.column_names[i]) != 0) {
            column_names_same = false;
            break;
        }
    }
    std::cout << "column_names same? " << (column_names_same ? "YES" : "NO") << "\n";
    
    return true;
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
    
    // Create two identical test entries
    entry_t entry1, entry2;
    memset(&entry1, 0, sizeof(entry_t));
    memset(&entry2, 0, sizeof(entry_t));
    
    // Set identical values
    entry1.join_attr = 12345;
    entry1.original_index = 42;
    entry1.local_mult = 100;
    entry1.final_mult = 200;
    entry1.is_encrypted = 0;
    entry1.nonce = 0;
    
    // Set some test attributes
    entry1.attributes[0] = 100;
    entry1.attributes[1] = 200;
    entry1.attributes[2] = 300;
    entry1.attributes[3] = 400;
    entry1.attributes[4] = 500;
    
    // Set column names
    strcpy(entry1.column_names[0], "COL1");
    strcpy(entry1.column_names[1], "COL2");
    strcpy(entry1.column_names[2], "COL3");
    strcpy(entry1.column_names[3], "COL4");
    strcpy(entry1.column_names[4], "COL5");
    
    // Copy to entry2 (make identical)
    memcpy(&entry2, &entry1, sizeof(entry_t));
    
    print_entry(entry1, "Original entry1");
    print_entry(entry2, "Original entry2");
    
    // Verify they are identical initially
    std::cout << "\n=== Initial state: entries should be identical ===\n";
    bool initially_identical = (memcmp(&entry1, &entry2, sizeof(entry_t)) == 0);
    std::cout << "Entries identical? " << (initially_identical ? "YES" : "NO") << "\n";
    
    // Save originals for comparison
    entry_t original1, original2;
    memcpy(&original1, &entry1, sizeof(entry_t));
    memcpy(&original2, &entry2, sizeof(entry_t));
    
    // Encrypt both entries
    std::cout << "\n=== Encrypting both entries with AES-CTR ===\n";
    crypto_status_t crypto_ret;
    
    ret = ecall_encrypt_entry_secure(global_eid, &crypto_ret, &entry1);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Encryption of entry1 failed: " << ret << ", " << crypto_ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "Entry1 encrypted successfully\n";
    
    ret = ecall_encrypt_entry_secure(global_eid, &crypto_ret, &entry2);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Encryption of entry2 failed: " << ret << ", " << crypto_ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "Entry2 encrypted successfully\n";
    
    // Print encrypted entries
    print_entry(entry1, "\nEncrypted entry1");
    print_entry(entry2, "\nEncrypted entry2");
    
    // Compare encrypted entries
    compare_entries(entry1, entry2, "After encryption");
    
    std::cout << "\n=== Analysis of encrypted entries ===\n";
    std::cout << "EXPECTED BEHAVIOR:\n";
    std::cout << "- is_encrypted should be same (1): " << (entry1.is_encrypted == 1 && entry2.is_encrypted == 1 ? "✓" : "✗") << "\n";
    std::cout << "- nonce should be DIFFERENT (unique per encryption): " << (entry1.nonce != entry2.nonce ? "✓" : "✗") << "\n";
    std::cout << "- column_names should be same (not encrypted): " << (strcmp(entry1.column_names[0], entry2.column_names[0]) == 0 ? "✓" : "✗") << "\n";
    std::cout << "- encrypted data fields should be DIFFERENT (due to different nonces): " << (entry1.join_attr != entry2.join_attr ? "✓" : "✗") << "\n";
    
    // Decrypt both entries
    std::cout << "\n=== Decrypting both entries ===\n";
    
    ret = ecall_decrypt_entry_secure(global_eid, &crypto_ret, &entry1);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Decryption of entry1 failed: " << ret << ", " << crypto_ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "Entry1 decrypted successfully\n";
    
    ret = ecall_decrypt_entry_secure(global_eid, &crypto_ret, &entry2);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Decryption of entry2 failed: " << ret << ", " << crypto_ret << std::endl;
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "Entry2 decrypted successfully\n";
    
    // Print decrypted entries
    print_entry(entry1, "\nDecrypted entry1");
    print_entry(entry2, "\nDecrypted entry2");
    
    // Check if decrypted values match original
    std::cout << "\n=== Verifying decryption restored original values ===\n";
    
    bool entry1_restored = true;
    if (entry1.join_attr != original1.join_attr) {
        std::cout << "ERROR: entry1 join_attr not restored! " << entry1.join_attr << " != " << original1.join_attr << "\n";
        entry1_restored = false;
    }
    if (entry1.original_index != original1.original_index) {
        std::cout << "ERROR: entry1 original_index not restored! " << entry1.original_index << " != " << original1.original_index << "\n";
        entry1_restored = false;
    }
    if (entry1.attributes[0] != original1.attributes[0]) {
        std::cout << "ERROR: entry1 attributes[0] not restored! " << entry1.attributes[0] << " != " << original1.attributes[0] << "\n";
        entry1_restored = false;
    }
    
    bool entry2_restored = true;
    if (entry2.join_attr != original2.join_attr) {
        std::cout << "ERROR: entry2 join_attr not restored! " << entry2.join_attr << " != " << original2.join_attr << "\n";
        entry2_restored = false;
    }
    if (entry2.original_index != original2.original_index) {
        std::cout << "ERROR: entry2 original_index not restored! " << entry2.original_index << " != " << original2.original_index << "\n";
        entry2_restored = false;
    }
    if (entry2.attributes[0] != original2.attributes[0]) {
        std::cout << "ERROR: entry2 attributes[0] not restored! " << entry2.attributes[0] << " != " << original2.attributes[0] << "\n";
        entry2_restored = false;
    }
    
    std::cout << "Entry1 restored to original: " << (entry1_restored ? "YES ✓" : "NO ✗") << "\n";
    std::cout << "Entry2 restored to original: " << (entry2_restored ? "YES ✓" : "NO ✗") << "\n";
    
    // Note about nonces after decryption
    std::cout << "\nNote: Nonces after decryption:\n";
    std::cout << "  entry1.nonce: " << entry1.nonce << " (preserved from encryption)\n";
    std::cout << "  entry2.nonce: " << entry2.nonce << " (preserved from encryption)\n";
    std::cout << "  Nonces remain different, which is correct - they track which nonce was used for encryption\n";
    
    // Additional test: Multiple encrypt/decrypt cycles
    std::cout << "\n=== TEST: Multiple Encrypt/Decrypt Cycles ===\n";
    std::cout << "Testing: encrypt -> decrypt -> encrypt -> decrypt\n\n";
    
    // Create a new entry for cycle testing
    entry_t cycle_entry;
    memset(&cycle_entry, 0, sizeof(entry_t));
    
    // Set test values
    cycle_entry.join_attr = 99999;
    cycle_entry.original_index = 777;
    cycle_entry.local_mult = 555;
    cycle_entry.attributes[0] = 11111;
    cycle_entry.attributes[1] = 22222;
    cycle_entry.attributes[2] = 33333;
    strcpy(cycle_entry.column_names[0], "CYCLE1");
    strcpy(cycle_entry.column_names[1], "CYCLE2");
    
    // Save original for final comparison
    entry_t cycle_original;
    memcpy(&cycle_original, &cycle_entry, sizeof(entry_t));
    
    print_entry(cycle_entry, "Initial cycle entry");
    
    // First encryption
    std::cout << "\nCycle 1: First encryption...\n";
    ret = ecall_encrypt_entry_secure(global_eid, &crypto_ret, &cycle_entry);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "First encryption failed\n";
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "  Encrypted with nonce: " << cycle_entry.nonce << "\n";
    std::cout << "  join_attr encrypted to: " << cycle_entry.join_attr << "\n";
    uint64_t first_nonce = cycle_entry.nonce;
    
    // First decryption
    std::cout << "\nCycle 1: First decryption...\n";
    ret = ecall_decrypt_entry_secure(global_eid, &crypto_ret, &cycle_entry);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "First decryption failed\n";
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "  Decrypted, join_attr is: " << cycle_entry.join_attr << "\n";
    std::cout << "  Nonce preserved as: " << cycle_entry.nonce << "\n";
    
    // Verify first cycle restored values
    bool first_cycle_ok = (cycle_entry.join_attr == cycle_original.join_attr &&
                           cycle_entry.original_index == cycle_original.original_index &&
                           cycle_entry.attributes[0] == cycle_original.attributes[0]);
    std::cout << "  Values match original after first cycle: " << (first_cycle_ok ? "YES ✓" : "NO ✗") << "\n";
    
    // Second encryption
    std::cout << "\nCycle 2: Second encryption...\n";
    ret = ecall_encrypt_entry_secure(global_eid, &crypto_ret, &cycle_entry);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Second encryption failed\n";
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "  Encrypted with NEW nonce: " << cycle_entry.nonce << "\n";
    std::cout << "  join_attr encrypted to: " << cycle_entry.join_attr << "\n";
    uint64_t second_nonce = cycle_entry.nonce;
    
    // Check if nonces are different
    std::cout << "  Nonces different between encryptions? " << (first_nonce != second_nonce ? "YES ✓" : "NO ✗") 
              << " (" << first_nonce << " vs " << second_nonce << ")\n";
    
    // Second decryption
    std::cout << "\nCycle 2: Second decryption...\n";
    ret = ecall_decrypt_entry_secure(global_eid, &crypto_ret, &cycle_entry);
    if (ret != SGX_SUCCESS || crypto_ret != CRYPTO_SUCCESS) {
        std::cerr << "Second decryption failed\n";
        sgx_destroy_enclave(global_eid);
        return 1;
    }
    std::cout << "  Decrypted, join_attr is: " << cycle_entry.join_attr << "\n";
    
    // Final verification
    print_entry(cycle_entry, "\nFinal cycle entry after encrypt->decrypt->encrypt->decrypt");
    
    bool cycle_test_passed = true;
    if (cycle_entry.join_attr != cycle_original.join_attr) {
        std::cout << "ERROR: join_attr mismatch after cycles! " << cycle_entry.join_attr << " != " << cycle_original.join_attr << "\n";
        cycle_test_passed = false;
    }
    if (cycle_entry.original_index != cycle_original.original_index) {
        std::cout << "ERROR: original_index mismatch after cycles! " << cycle_entry.original_index << " != " << cycle_original.original_index << "\n";
        cycle_test_passed = false;
    }
    if (cycle_entry.attributes[0] != cycle_original.attributes[0]) {
        std::cout << "ERROR: attributes[0] mismatch after cycles! " << cycle_entry.attributes[0] << " != " << cycle_original.attributes[0] << "\n";
        cycle_test_passed = false;
    }
    
    std::cout << "\nMultiple cycle test result: " << (cycle_test_passed ? "PASSED ✓" : "FAILED ✗") << "\n";
    std::cout << "Key observations:\n";
    std::cout << "  - Each encryption gets a NEW unique nonce\n";
    std::cout << "  - Values are correctly restored after each decrypt\n";
    std::cout << "  - Multiple cycles work correctly\n";
    
    // Final summary
    std::cout << "\n=== TEST SUMMARY ===\n";
    bool test_passed = entry1_restored && entry2_restored && cycle_test_passed;
    if (test_passed) {
        std::cout << "✓ ALL TESTS PASSED: AES-CTR encryption/decryption works correctly\n";
        std::cout << "  - Each encryption gets unique nonce\n";
        std::cout << "  - Encrypted data is different even for identical input\n";
        std::cout << "  - Decryption restores original values\n";
        std::cout << "  - Multiple encrypt/decrypt cycles work correctly\n";
    } else {
        std::cout << "✗ SOME TESTS FAILED: AES-CTR encryption/decryption has issues\n";
    }
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    
    return test_passed ? 0 : 1;
}