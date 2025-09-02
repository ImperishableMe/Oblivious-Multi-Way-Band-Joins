/**
 * Verify that encrypted tables can be loaded and decrypted correctly
 */
#include <iostream>
#include "../../src/io/table_io.h"
#include "../../src/crypto/crypto_utils.h"
#include "Enclave_u.h"
#include "sgx_urts.h"

sgx_enclave_id_t global_eid = 0;

int main() {
    // Initialize enclave
    sgx_status_t ret = sgx_create_enclave("../enclave.signed.so", SGX_DEBUG_FLAG,
                                          NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave" << std::endl;
        return 1;
    }
    
    // Test loading encrypted CSV
    std::cout << "Loading encrypted customer table..." << std::endl;
    Table encrypted = TableIO::load_csv("../../../encrypted/data_0_001/customer.csv");
    
    std::cout << "Loaded " << encrypted.size() << " rows" << std::endl;
    
    // Check encryption status
    auto status = encrypted.get_encryption_status();
    if (status == Table::ENCRYPTED) {
        std::cout << "✓ Table correctly detected as ENCRYPTED" << std::endl;
        
        // Check first entry has nonce
        const Entry& e = encrypted.get_entry(0);
        if (e.nonce != 0) {
            std::cout << "✓ Nonce present: " << e.nonce << std::endl;
        } else {
            std::cout << "✗ Nonce missing!" << std::endl;
        }
        
        // Decrypt first entry
        Entry test_entry = encrypted.get_entry(0);
        crypto_status_t ret = CryptoUtils::decrypt_entry(test_entry, global_eid);
        if (ret == CRYPTO_SUCCESS) {
            std::cout << "✓ Successfully decrypted first entry" << std::endl;
            std::cout << "  C_CUSTKEY: " << test_entry.attributes[0] << std::endl;
        } else {
            std::cout << "✗ Failed to decrypt" << std::endl;
        }
    } else {
        std::cout << "✗ Table not detected as encrypted!" << std::endl;
    }
    
    // Clean up
    sgx_destroy_enclave(global_eid);
    return 0;
}