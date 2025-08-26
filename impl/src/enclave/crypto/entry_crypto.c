#include "entry_crypto.h"
#include <stddef.h>  // For offsetof
#include <stdint.h>  // For uint8_t

/**
 * Internal XOR function that performs the actual encryption/decryption
 * XORs the entire struct EXCEPT:
 * - is_encrypted flag (needed to track encryption state)
 * - column_names array (metadata, not sensitive)
 * This approach ensures all current and future fields are encrypted by default
 */
void xor_entry_fields(entry_t* entry, int32_t key) {
    uint8_t* bytes = (uint8_t*)entry;
    size_t struct_size = sizeof(entry_t);
    
    // Calculate offsets for excluded fields
    size_t is_encrypted_offset = offsetof(entry_t, is_encrypted);
    size_t is_encrypted_end = is_encrypted_offset + sizeof(uint8_t);
    
    size_t column_names_offset = offsetof(entry_t, column_names);
    size_t column_names_end = column_names_offset + (MAX_ATTRIBUTES * MAX_COLUMN_NAME_LEN);
    
    // XOR every byte except the excluded fields
    for (size_t i = 0; i < struct_size; i++) {
        // Skip is_encrypted flag
        if (i >= is_encrypted_offset && i < is_encrypted_end) {
            continue;
        }
        
        // Skip column_names array
        if (i >= column_names_offset && i < column_names_end) {
            continue;
        }
        
        // XOR this byte with rotating key bytes
        bytes[i] ^= (key >> ((i % 4) * 8)) & 0xFF;
    }
    
    // Note: This approach encrypts ALL fields by default except those explicitly excluded
    // If new fields are added to entry_t, they will automatically be encrypted
}

/**
 * Encrypt an entry with safety checks
 */
crypto_status_t encrypt_entry(entry_t* entry, int32_t key) {
    if (!entry) {
        return CRYPTO_INVALID_PARAM;
    }
    
    // Check if already encrypted
    if (entry->is_encrypted) {
        // TODO: Add warning system here
        // For now, we just return error status
        return CRYPTO_ALREADY_ENCRYPTED;
    }
    
    // Perform XOR encryption
    xor_entry_fields(entry, key);
    
    // Mark as encrypted
    entry->is_encrypted = 1;
    
    return CRYPTO_SUCCESS;
}

/**
 * Decrypt an entry with safety checks
 */
crypto_status_t decrypt_entry(entry_t* entry, int32_t key) {
    if (!entry) {
        return CRYPTO_INVALID_PARAM;
    }
    
    // Check if not encrypted
    if (!entry->is_encrypted) {
        // TODO: Add warning system here
        // For now, we just return error status
        return CRYPTO_NOT_ENCRYPTED;
    }
    
    // Perform XOR decryption (same as encryption)
    xor_entry_fields(entry, key);
    
    // Mark as not encrypted
    entry->is_encrypted = 0;
    
    return CRYPTO_SUCCESS;
}

/**
 * Batch encrypt multiple entries
 */
crypto_status_t encrypt_entries(entry_t* entries, size_t count, int32_t key) {
    if (!entries || count == 0) {
        return CRYPTO_INVALID_PARAM;
    }
    
    // First pass: check if any are already encrypted
    for (size_t i = 0; i < count; i++) {
        if (entries[i].is_encrypted) {
            // TODO: Add warning with specific index
            return CRYPTO_ALREADY_ENCRYPTED;
        }
    }
    
    // Second pass: encrypt all
    for (size_t i = 0; i < count; i++) {
        crypto_status_t status = encrypt_entry(&entries[i], key);
        if (status != CRYPTO_SUCCESS) {
            // This shouldn't happen since we checked, but handle it anyway
            return status;
        }
    }
    
    return CRYPTO_SUCCESS;
}

/**
 * Batch decrypt multiple entries
 */
crypto_status_t decrypt_entries(entry_t* entries, size_t count, int32_t key) {
    if (!entries || count == 0) {
        return CRYPTO_INVALID_PARAM;
    }
    
    // First pass: check if any are not encrypted
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].is_encrypted) {
            // TODO: Add warning with specific index
            return CRYPTO_NOT_ENCRYPTED;
        }
    }
    
    // Second pass: decrypt all
    for (size_t i = 0; i < count; i++) {
        crypto_status_t status = decrypt_entry(&entries[i], key);
        if (status != CRYPTO_SUCCESS) {
            // This shouldn't happen since we checked, but handle it anyway
            return status;
        }
    }
    
    return CRYPTO_SUCCESS;
}