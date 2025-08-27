#include "entry_crypto.h"

/**
 * NOTE: XOR-based encryption has been deprecated and removed.
 * All encryption/decryption is now handled by AES functions in aes_crypto.c
 * 
 * The ecalls ecall_encrypt_entry and ecall_decrypt_entry in Enclave.cpp
 * directly use aes_encrypt_entry and aes_decrypt_entry.
 * 
 * This file is kept as an empty stub to avoid breaking the build system.
 */

// Stub implementations that should never be called
crypto_status_t encrypt_entry(entry_t* entry, int32_t key) {
    (void)entry;
    (void)key;
    return CRYPTO_OPERATION_FAILED;
}

crypto_status_t decrypt_entry(entry_t* entry, int32_t key) {
    (void)entry;
    (void)key;
    return CRYPTO_OPERATION_FAILED;
}

crypto_status_t encrypt_entries(entry_t* entries, size_t count, int32_t key) {
    (void)entries;
    (void)count;
    (void)key;
    return CRYPTO_OPERATION_FAILED;
}

crypto_status_t decrypt_entries(entry_t* entries, size_t count, int32_t key) {
    (void)entries;
    (void)count;
    (void)key;
    return CRYPTO_OPERATION_FAILED;
}