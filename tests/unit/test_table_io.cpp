/**
 * Test for Table I/O with nonce handling
 * 
 * Tests:
 * 1. Load unencrypted table from CSV
 * 2. Encrypt table and save with nonces
 * 3. Load encrypted table and verify nonces preserved
 * 4. Decrypt table
 * 5. Save decrypted table and compare with original
 * 6. Test encryption status detection
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <cstdio>
#include "../app/utils/table_io.h"
#include "../app/crypto_utils.h"
#include "../app/Enclave_u.h"
#include "sgx_urts.h"

sgx_enclave_id_t global_eid = 0;

int initialize_enclave() {
    sgx_status_t ret = sgx_create_enclave("../enclave.signed.so", SGX_DEBUG_FLAG, 
                                          NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return -1;
    }
    return 0;
}

void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
    }
}

bool compare_files(const std::string& file1, const std::string& file2) {
    std::ifstream f1(file1);
    std::ifstream f2(file2);
    
    if (!f1.is_open() || !f2.is_open()) {
        return false;
    }
    
    std::string line1, line2;
    while (std::getline(f1, line1) && std::getline(f2, line2)) {
        if (line1 != line2) {
            return false;
        }
    }
    
    // Check both streams have no more data
    bool f1_done = !std::getline(f1, line1);
    bool f2_done = !std::getline(f2, line2);
    
    return f1_done && f2_done;
}

void test_table_io_with_nonce() {
    std::cout << "\n=== Testing Table I/O with Nonce Handling ===" << std::endl;
    
    // Create a test CSV file
    const std::string original_csv = "test_original.csv";
    const std::string encrypted_csv = "test_encrypted.csv";
    const std::string decrypted_csv = "test_decrypted.csv";
    
    // Create original CSV
    {
        std::ofstream file(original_csv);
        file << "ID,NAME,VALUE\n";
        file << "1,100,1000\n";
        file << "2,200,2000\n";
        file << "3,300,3000\n";
        file.close();
    }
    
    // 1. Load unencrypted table
    std::cout << "Loading unencrypted CSV..." << std::endl;
    Table table = TableIO::load_csv(original_csv);
    std::cout << "  Loaded " << table.size() << " rows" << std::endl;
    
    // Check encryption status
    auto status = table.get_encryption_status();
    if (status == Table::UNENCRYPTED) {
        std::cout << "  ✓ Table correctly detected as UNENCRYPTED" << std::endl;
    } else {
        std::cout << "  ✗ Failed: Table should be UNENCRYPTED" << std::endl;
    }
    
    // Verify data
    if (table.size() == 3) {
        const Entry& e = table.get_entry(0);
        if (!e.is_encrypted && e.nonce == 0 && e.attributes[0] == 1) {
            std::cout << "  ✓ Unencrypted data loaded correctly" << std::endl;
        } else {
            std::cout << "  ✗ Data not loaded correctly" << std::endl;
        }
    }
    
    // 2. Encrypt table entries
    std::cout << "\nEncrypting table..." << std::endl;
    for (size_t i = 0; i < table.size(); ++i) {
        Entry& entry = table.get_entry(i);
        crypto_status_t ret = CryptoUtils::encrypt_entry(entry, global_eid);
        if (ret != CRYPTO_SUCCESS) {
            std::cout << "  ✗ Encryption failed for entry " << i << std::endl;
            return;
        }
    }
    
    // Check encryption status after encryption
    status = table.get_encryption_status();
    if (status == Table::ENCRYPTED) {
        std::cout << "  ✓ Table correctly detected as ENCRYPTED" << std::endl;
    } else {
        std::cout << "  ✗ Failed: Table should be ENCRYPTED after encryption" << std::endl;
    }
    
    // Verify nonces were generated
    bool nonces_valid = true;
    for (size_t i = 0; i < table.size(); ++i) {
        if (table.get_entry(i).nonce == 0) {
            nonces_valid = false;
            break;
        }
    }
    if (nonces_valid) {
        std::cout << "  ✓ Nonces generated for all entries" << std::endl;
    } else {
        std::cout << "  ✗ Some entries missing nonces" << std::endl;
    }
    
    // 3. Save encrypted table with nonces
    std::cout << "\nSaving encrypted CSV with nonces..." << std::endl;
    TableIO::save_encrypted_csv(table, encrypted_csv, global_eid);
    std::cout << "  ✓ Saved to " << encrypted_csv << std::endl;
    
    // 4. Load encrypted table
    std::cout << "\nLoading encrypted CSV..." << std::endl;
    Table loaded_encrypted = TableIO::load_csv(encrypted_csv);
    std::cout << "  Loaded " << loaded_encrypted.size() << " rows" << std::endl;
    
    // Check that it's detected as encrypted
    status = loaded_encrypted.get_encryption_status();
    if (status == Table::ENCRYPTED) {
        std::cout << "  ✓ Loaded table correctly detected as ENCRYPTED" << std::endl;
    } else {
        std::cout << "  ✗ Failed: Loaded table should be ENCRYPTED" << std::endl;
    }
    
    // Verify nonces were preserved
    bool nonces_preserved = true;
    for (size_t i = 0; i < table.size() && i < loaded_encrypted.size(); ++i) {
        if (table.get_entry(i).nonce != loaded_encrypted.get_entry(i).nonce) {
            nonces_preserved = false;
            std::cout << "  Nonce mismatch at entry " << i << ": " 
                     << table.get_entry(i).nonce << " != " 
                     << loaded_encrypted.get_entry(i).nonce << std::endl;
            break;
        }
    }
    if (nonces_preserved) {
        std::cout << "  ✓ Nonces preserved correctly" << std::endl;
    } else {
        std::cout << "  ✗ Nonces not preserved" << std::endl;
    }
    
    // 5. Decrypt the loaded table
    std::cout << "\nDecrypting loaded table..." << std::endl;
    for (size_t i = 0; i < loaded_encrypted.size(); ++i) {
        Entry& entry = loaded_encrypted.get_entry(i);
        crypto_status_t ret = CryptoUtils::decrypt_entry(entry, global_eid);
        if (ret != CRYPTO_SUCCESS) {
            std::cout << "  ✗ Decryption failed for entry " << i << std::endl;
            return;
        }
    }
    std::cout << "  ✓ All entries decrypted" << std::endl;
    
    // Check decrypted status
    status = loaded_encrypted.get_encryption_status();
    if (status == Table::UNENCRYPTED) {
        std::cout << "  ✓ Table correctly detected as UNENCRYPTED after decryption" << std::endl;
    } else {
        std::cout << "  ✗ Failed: Table should be UNENCRYPTED after decryption" << std::endl;
    }
    
    // 6. Save decrypted table
    std::cout << "\nSaving decrypted CSV..." << std::endl;
    TableIO::save_csv(loaded_encrypted, decrypted_csv);
    std::cout << "  ✓ Saved to " << decrypted_csv << std::endl;
    
    // 7. Compare original and decrypted files
    std::cout << "\nComparing original and decrypted files..." << std::endl;
    if (compare_files(original_csv, decrypted_csv)) {
        std::cout << "  ✓ Files are identical - encryption/decryption cycle successful!" << std::endl;
    } else {
        std::cout << "  ✗ Files differ - something went wrong" << std::endl;
    }
    
    // Test mixed encryption status
    std::cout << "\nTesting mixed encryption status..." << std::endl;
    Table mixed_table("mixed");
    Entry e1, e2;
    e1.is_encrypted = true;
    e2.is_encrypted = false;
    mixed_table.add_entry(e1);
    mixed_table.add_entry(e2);
    
    status = mixed_table.get_encryption_status();
    if (status == Table::MIXED) {
        std::cout << "  ✓ Mixed encryption status detected correctly" << std::endl;
    } else {
        std::cout << "  ✗ Failed to detect mixed encryption status" << std::endl;
    }
    
    // Clean up test files
    std::remove(original_csv.c_str());
    std::remove(encrypted_csv.c_str());
    std::remove(decrypted_csv.c_str());
}

int main() {
    std::cout << "Table I/O Test with Nonce Handling" << std::endl;
    std::cout << "===================================" << std::endl;
    
    // Initialize enclave
    if (initialize_enclave() < 0) {
        std::cerr << "Failed to initialize enclave" << std::endl;
        return 1;
    }
    
    // Run tests
    test_table_io_with_nonce();
    
    // Destroy enclave
    destroy_enclave();
    
    std::cout << "\n=== All tests completed ===" << std::endl;
    return 0;
}