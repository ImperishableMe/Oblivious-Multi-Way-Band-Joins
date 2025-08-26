#include <iostream>
#include <cstring>
#include "sgx_urts.h"
#include "Enclave_u.h"
#include "crypto_utils.h"
#include "types.h"

/* Global enclave ID */
sgx_enclave_id_t global_eid = 0;

/* Initialize the enclave */
int initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    /* Call sgx_create_enclave to initialize an enclave instance */
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: 0x" << std::hex << ret << std::endl;
        return -1;
    }
    
    std::cout << "Enclave created successfully. ID: " << global_eid << std::endl;
    return 0;
}

/* Destroy the enclave */
void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
        std::cout << "Enclave destroyed." << std::endl;
    }
}

/* Simple test function */
void test_encryption() {
    std::cout << "\n=== Testing Encryption through SGX ===" << std::endl;
    
    Entry entry;
    entry.original_index = 42;
    entry.local_mult = 100;
    entry.join_attr = 3.14159;
    entry.is_encrypted = false;
    entry.field_type = SOURCE;
    entry.equality_type = EQ;
    
    // Add some test data
    for (int i = 0; i < 5; i++) {
        entry.attributes.push_back(i * 1.5);
        entry.column_names.push_back("col" + std::to_string(i));
    }
    
    uint32_t key = 0xDEADBEEF;
    
    // Test encryption
    std::cout << "Original index: " << entry.original_index << std::endl;
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, key, global_eid);
    if (status == CRYPTO_SUCCESS) {
        std::cout << "Encryption successful. Encrypted index: " << entry.original_index << std::endl;
    } else {
        std::cerr << "Encryption failed with status: " << status << std::endl;
    }
    
    // Test decryption
    status = CryptoUtils::decrypt_entry(entry, key, global_eid);
    if (status == CRYPTO_SUCCESS) {
        std::cout << "Decryption successful. Decrypted index: " << entry.original_index << std::endl;
    } else {
        std::cerr << "Decryption failed with status: " << status << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "SGX Encryption Test Application" << std::endl;
    std::cout << "================================" << std::endl;
    
    /* Initialize the enclave */
    if (initialize_enclave() < 0) {
        std::cerr << "Enclave initialization failed!" << std::endl;
        return -1;
    }
    
    /* Run test if requested */
    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        test_encryption();
    } else {
        std::cout << "Enclave ready. Use './sgx_app test' to run encryption test." << std::endl;
    }
    
    /* Destroy the enclave */
    destroy_enclave();
    
    return 0;
}