#include "entry_crypto.h"
#include "enclave_types.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/**
 * Standalone test for encryption without SGX
 * Tests the core encryption logic directly
 */

void test_encryption_exclusions() {
    printf("Testing encryption exclusions (is_encrypted and column_names)...\n");
    
    entry_t entry1, entry2;
    
    // Fill both with same pattern
    memset(&entry1, 0xAA, sizeof(entry_t));
    memset(&entry2, 0xAA, sizeof(entry_t));
    
    // Set is_encrypted flags
    entry1.is_encrypted = 0;
    entry2.is_encrypted = 0;
    
    // Encrypt one directly (bypassing safety checks for testing)
    xor_entry_fields(&entry1, 0xDEADBEEF);
    entry1.is_encrypted = 1;  // Manually set after encryption
    
    // Compare byte by byte
    uint8_t* p1 = (uint8_t*)&entry1;
    uint8_t* p2 = (uint8_t*)&entry2;
    
    size_t unchanged_bytes = 0;
    size_t is_encrypted_offset = offsetof(entry_t, is_encrypted);
    size_t column_names_offset = offsetof(entry_t, column_names);
    size_t column_names_size = MAX_ATTRIBUTES * MAX_COLUMN_NAME_LEN;
    
    printf("Structure layout:\n");
    printf("  sizeof(entry_t) = %zu bytes\n", sizeof(entry_t));
    printf("  is_encrypted at offset %zu\n", is_encrypted_offset);
    printf("  column_names at offset %zu (size %zu)\n", column_names_offset, column_names_size);
    
    // Count unchanged bytes in excluded regions
    size_t unchanged_in_column_names = 0;
    int found_unexpected_unchanged = 0;
    
    for (size_t i = 0; i < sizeof(entry_t); i++) {
        if (p1[i] == p2[i]) {
            unchanged_bytes++;
            
            // Check if this byte is in column_names
            if (i >= column_names_offset && i < column_names_offset + column_names_size) {
                unchanged_in_column_names++;
            }
            // Check if it's NOT in an excluded region
            else if (i != is_encrypted_offset) {  // is_encrypted is manually set, so skip
                printf("  WARNING: Unchanged byte at offset %zu (not in excluded region)\n", i);
                found_unexpected_unchanged = 1;
            }
        }
    }
    
    printf("\nResults:\n");
    printf("  Total unchanged bytes: %zu\n", unchanged_bytes);
    printf("  Unchanged in column_names: %zu (expected %zu)\n", 
           unchanged_in_column_names, column_names_size);
    
    // Verify column_names array is completely unchanged
    assert(unchanged_in_column_names == column_names_size);
    
    if (found_unexpected_unchanged) {
        printf("ERROR: Found unexpected unchanged bytes outside excluded regions!\n");
        assert(0);
    }
    
    printf("✓ Encryption exclusion test passed\n");
}

void test_roundtrip() {
    printf("\nTesting encryption roundtrip...\n");
    
    entry_t original, encrypted, decrypted;
    
    // Initialize with test data
    memset(&original, 0, sizeof(entry_t));
    original.field_type = SOURCE;
    original.equality_type = EQ;
    original.is_encrypted = 0;
    original.join_attr = 3.14159;
    original.original_index = 42;
    original.local_mult = 7;
    strcpy(original.column_names[0], "test_col");
    
    // Copy for encryption
    encrypted = original;
    
    // Encrypt
    uint32_t key = 0xDEADBEEF;
    crypto_status_t status = encrypt_entry(&encrypted, key);
    assert(status == CRYPTO_SUCCESS);
    assert(encrypted.is_encrypted == 1);
    assert(encrypted.original_index != 42);  // Should be different
    assert(encrypted.field_type != SOURCE);  // Should be encrypted
    assert(strcmp(encrypted.column_names[0], "test_col") == 0);  // Should be unchanged
    
    // Decrypt
    decrypted = encrypted;
    status = decrypt_entry(&decrypted, key);
    assert(status == CRYPTO_SUCCESS);
    assert(decrypted.is_encrypted == 0);
    assert(decrypted.original_index == 42);
    assert(decrypted.field_type == SOURCE);
    assert(decrypted.join_attr == original.join_attr);
    assert(strcmp(decrypted.column_names[0], "test_col") == 0);
    
    printf("✓ Roundtrip test passed\n");
}

void test_double_encryption_prevention() {
    printf("\nTesting double encryption prevention...\n");
    
    entry_t entry;
    memset(&entry, 0, sizeof(entry_t));
    entry.original_index = 100;
    entry.is_encrypted = 0;
    
    // First encryption should succeed
    crypto_status_t status = encrypt_entry(&entry, 0x12345678);
    assert(status == CRYPTO_SUCCESS);
    assert(entry.is_encrypted == 1);
    
    // Second encryption should fail
    status = encrypt_entry(&entry, 0x12345678);
    assert(status == CRYPTO_ALREADY_ENCRYPTED);
    
    // Decrypt should succeed
    status = decrypt_entry(&entry, 0x12345678);
    assert(status == CRYPTO_SUCCESS);
    assert(entry.is_encrypted == 0);
    assert(entry.original_index == 100);
    
    // Double decrypt should fail
    status = decrypt_entry(&entry, 0x12345678);
    assert(status == CRYPTO_NOT_ENCRYPTED);
    
    printf("✓ Double encryption prevention test passed\n");
}

int main() {
    printf("=== Running Standalone Encryption Tests ===\n");
    
    test_encryption_exclusions();
    test_roundtrip();
    test_double_encryption_prevention();
    
    printf("\n✓✓✓ All standalone tests passed! ✓✓✓\n");
    return 0;
}