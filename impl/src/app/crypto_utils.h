#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include "types.h"
#include "../enclave/crypto/entry_crypto.h"
#include "sgx_urts.h"
#include <string>

/**
 * Application-side crypto utilities for Entry encryption/decryption
 * These functions handle the conversion between C++ Entry and C entry_t
 * and call the appropriate SGX ecalls
 */
class CryptoUtils {
public:
    /**
     * Encrypt a single entry with safety checks
     * @param entry Entry to encrypt (modified in-place)
     * @param key Encryption key
     * @param eid Enclave ID
     * @return Status code indicating success or failure
     */
    static crypto_status_t encrypt_entry(Entry& entry, uint32_t key, sgx_enclave_id_t eid);
    
    /**
     * Decrypt a single entry with safety checks
     * @param entry Entry to decrypt (modified in-place)
     * @param key Decryption key (must match encryption key)
     * @param eid Enclave ID
     * @return Status code indicating success or failure
     */
    static crypto_status_t decrypt_entry(Entry& entry, uint32_t key, sgx_enclave_id_t eid);
    
    /**
     * Encrypt an entire table
     * @param table Table to encrypt (modified in-place)
     * @param key Encryption key
     * @param eid Enclave ID
     * @return Status code indicating success or failure
     */
    static crypto_status_t encrypt_table(Table& table, uint32_t key, sgx_enclave_id_t eid);
    
    /**
     * Decrypt an entire table
     * @param table Table to decrypt (modified in-place)
     * @param key Decryption key
     * @param eid Enclave ID
     * @return Status code indicating success or failure
     */
    static crypto_status_t decrypt_table(Table& table, uint32_t key, sgx_enclave_id_t eid);
    
    /**
     * Generate a random encryption key
     * @return Random 32-bit key
     */
    static uint32_t generate_key();
    
    /**
     * Get human-readable error message for crypto status
     * @param status Crypto status code
     * @return Error message string
     */
    static std::string get_status_message(crypto_status_t status);

private:
    /**
     * Log crypto errors (placeholder for warning system)
     * @param status Error status
     * @param operation Operation that failed
     */
    static void log_crypto_error(crypto_status_t status, const std::string& operation);
};

#endif // CRYPTO_UTILS_H