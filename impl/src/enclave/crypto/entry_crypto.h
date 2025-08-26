#ifndef ENTRY_CRYPTO_H
#define ENTRY_CRYPTO_H

#include "../enclave_types.h"
#include <stdbool.h>
#include <stddef.h>  // For size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encryption status codes
 */
typedef enum {
    CRYPTO_SUCCESS = 0,           // Operation successful
    CRYPTO_ALREADY_ENCRYPTED = 1, // Attempted to encrypt already encrypted entry
    CRYPTO_NOT_ENCRYPTED = 2,     // Attempted to decrypt non-encrypted entry
    CRYPTO_INVALID_PARAM = 3,     // Invalid parameter (e.g., NULL pointer)
    CRYPTO_OPERATION_FAILED = 4   // Encryption/decryption operation failed
} crypto_status_t;

/**
 * Encrypt an entry using XOR with safety checks
 * @param entry Entry to encrypt (modified in-place)
 * @param key Encryption key
 * @return Status code indicating success or failure reason
 */
crypto_status_t encrypt_entry(entry_t* entry, int32_t key);

/**
 * Decrypt an entry using XOR with safety checks
 * @param entry Entry to decrypt (modified in-place)
 * @param key Decryption key (must match encryption key)
 * @return Status code indicating success or failure reason
 */
crypto_status_t decrypt_entry(entry_t* entry, int32_t key);

/**
 * Batch encrypt multiple entries
 * @param entries Array of entries to encrypt
 * @param count Number of entries
 * @param key Encryption key
 * @return Status code (fails if any entry is already encrypted)
 */
crypto_status_t encrypt_entries(entry_t* entries, size_t count, int32_t key);

/**
 * Batch decrypt multiple entries
 * @param entries Array of entries to decrypt
 * @param count Number of entries
 * @param key Decryption key
 * @return Status code (fails if any entry is not encrypted)
 */
crypto_status_t decrypt_entries(entry_t* entries, size_t count, int32_t key);

/**
 * Internal XOR function (used by both encrypt and decrypt)
 * This function performs the actual XOR operation on all sensitive fields
 * @param entry Entry to XOR
 * @param key XOR key
 */
void xor_entry_fields(entry_t* entry, int32_t key);

#ifdef __cplusplus
}
#endif

#endif // ENTRY_CRYPTO_H