#include "Enclave_t.h"
#include "enclave_types.h"
#include "entry_crypto.h"
#include "aes_crypto.h"
#include "core.h"

// ENCLAVE_BUILD is already defined in Makefile
#include "secure_key.h"

/**
 * Essential Ecall Implementations
 * Only 4 ecalls remain after batching optimization:
 * 1. encrypt_entry - For file I/O and debug
 * 2. decrypt_entry - For file I/O and debug  
 * 3. obtain_output_size - Get output size from last entry
 * 4. batch_dispatcher - Handles all 36+ batched operations
 */

crypto_status_t ecall_encrypt_entry(entry_t* entry) {
    // Use AES-CTR encryption with secure enclave key
    return aes_encrypt_entry(entry);
}

crypto_status_t ecall_decrypt_entry(entry_t* entry) {
    // Use AES-CTR decryption with secure enclave key
    return aes_decrypt_entry(entry);
}

void ecall_obtain_output_size(int32_t* retval, const entry_t* entry) {
    *retval = obtain_output_size(entry);
}

// Note: ecall_batch_dispatcher is implemented in enclave/batch/batch_dispatcher.c

// Note: ocall_debug_print is implemented in untrusted code (app side), not here