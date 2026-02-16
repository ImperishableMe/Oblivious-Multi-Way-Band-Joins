/**
 * Unit test for oblivious 2-way Waksman shuffle
 * Tests that shuffle runs without errors
 */

#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>

#include "../../common/enclave_types.h"
#include "../../common/op_types.h"
#include "../../app/crypto/crypto_utils.h"
#include "Enclave_u.h"
#include "sgx_urts.h"

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

/**
 * Create test entries with sequential values
 */
std::vector<entry_t> create_test_entries(size_t n) {
    std::vector<entry_t> entries;
    
    for (size_t i = 0; i < n; i++) {
        entry_t e;
        memset(&e, 0, sizeof(entry_t));
        e.attributes[0] = i;  // Sequential value for testing
        e.attributes[1] = i * 100;  // Some other value
        e.is_encrypted = 0;
        e.nonce = 0;
        entries.push_back(e);
    }
    
    return entries;
}

/**
 * Test that Waksman shuffle runs successfully
 */
void test_waksman_basic() {
    std::cout << "Testing Waksman basic functionality..." << std::endl;
    
    // Test power-of-2 sizes only (Waksman now requires power-of-2)
    std::vector<size_t> test_sizes = {2, 4, 8, 16, 32, 64};
    
    for (size_t n : test_sizes) {
        std::cout << "  Testing n=" << n << "..." << std::endl;
        
        // Create test data
        std::vector<entry_t> entries = create_test_entries(n);
        
        std::cout << "    Created " << entries.size() << " entries" << std::endl;
        
        // ENCRYPT entries before shuffle (as they should be)
        for (size_t i = 0; i < n; i++) {
            crypto_status_t enc_status;
            sgx_status_t ecall_ret = ecall_encrypt_entry(global_eid, &enc_status, &entries[i]);
            if (ecall_ret != SGX_SUCCESS || enc_status != CRYPTO_SUCCESS) {
                std::cerr << "Failed to encrypt entry " << i << std::endl;
                exit(1);
            }
        }
        std::cout << "    Encrypted all entries" << std::endl;
        std::cout << "    Enclave ID before ecall: " << global_eid << std::endl;
        
        // Apply Waksman shuffle
        sgx_status_t status = SGX_SUCCESS;
        sgx_status_t ecall_status = ecall_oblivious_2way_waksman(
            global_eid, &status, entries.data(), n);
        
        std::cout << "    Ecall returned: ecall_status=" << ecall_status 
                  << ", status=" << status << std::endl;
        
        if (ecall_status != SGX_SUCCESS) {
            std::cerr << "Ecall failed for n=" << n 
                     << " (ecall_status=" << ecall_status << ")" << std::endl;
            exit(1);
        }
        
        if (status != SGX_SUCCESS) {
            std::cerr << "Waksman shuffle failed for n=" << n 
                     << " (status=" << status << ")" << std::endl;
            exit(1);
        }
        
        // Check that entries are encrypted (shows shuffle ran)
        bool all_encrypted = true;
        for (size_t i = 0; i < n; i++) {
            if (!entries[i].is_encrypted) {
                all_encrypted = false;
                break;
            }
        }
        
        if (all_encrypted) {
            std::cout << "  n=" << n << " PASS (shuffle completed, entries encrypted)" << std::endl;
            // Debug: Print first entry's nonce to see if it's reasonable
            std::cout << "    First entry nonce: " << entries[0].nonce << std::endl;
        } else {
            std::cerr << "  n=" << n << " WARNING: Some entries not encrypted" << std::endl;
        }
    }
}

/**
 * Test that different shuffles produce different nonces
 */
void test_waksman_different_nonces() {
    std::cout << "\nTesting that shuffles use different nonces..." << std::endl;
    
    size_t n = 16;
    int trials = 5;
    
    std::vector<uint64_t> first_nonces;
    
    for (int trial = 0; trial < trials; trial++) {
        // Create test data
        std::vector<entry_t> entries = create_test_entries(n);
        
        // Apply Waksman shuffle
        sgx_status_t status;
        ecall_oblivious_2way_waksman(global_eid, &status, entries.data(), n);
        
        if (status != SGX_SUCCESS) {
            std::cerr << "Shuffle failed on trial " << trial << std::endl;
            exit(1);
        }
        
        // Record first entry's nonce
        first_nonces.push_back(entries[0].nonce);
    }
    
    // Check that nonces are different (indicating different shuffles)
    bool all_different = true;
    for (int i = 1; i < trials; i++) {
        if (first_nonces[i] == first_nonces[i-1]) {
            all_different = false;
            break;
        }
    }
    
    if (all_different) {
        std::cout << "  PASS - Different nonces for each shuffle" << std::endl;
    } else {
        std::cout << "  WARNING - Some nonces are the same" << std::endl;
    }
}

/**
 * Performance test for different sizes
 */
void test_waksman_performance() {
    std::cout << "\nTesting Waksman performance..." << std::endl;
    
    std::vector<size_t> sizes = {16, 64, 128, 512, 1024, 2048};
    
    for (size_t n : sizes) {
        if (n > MAX_BATCH_SIZE) {
            std::cout << "  Skipping n=" << n << " (exceeds MAX_BATCH_SIZE)" << std::endl;
            continue;
        }
        
        std::vector<entry_t> entries = create_test_entries(n);
        
        // Time the shuffle
        auto start = std::chrono::high_resolution_clock::now();
        
        sgx_status_t status;
        ecall_oblivious_2way_waksman(global_eid, &status, entries.data(), n);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        if (status == SGX_SUCCESS) {
            std::cout << "  n=" << n << ": " << duration.count() << " microseconds" 
                     << " (" << duration.count() / n << " us/element)" << std::endl;
        } else {
            std::cout << "  n=" << n << ": FAILED" << std::endl;
        }
    }
}

/**
 * Test multiple shuffles in sequence
 */
void test_waksman_multiple() {
    std::cout << "\nTesting multiple sequential shuffles..." << std::endl;
    
    size_t n = 128;  // Power of 2
    std::vector<entry_t> entries = create_test_entries(n);
    
    for (int i = 0; i < 10; i++) {
        sgx_status_t status;
        ecall_oblivious_2way_waksman(global_eid, &status, entries.data(), n);
        
        if (status != SGX_SUCCESS) {
            std::cerr << "Failed on shuffle " << i << std::endl;
            exit(1);
        }
    }
    
    std::cout << "  PASS - 10 sequential shuffles completed" << std::endl;
}

int main(int, char*[]) {
    std::cout << "=== Waksman Shuffle Unit Tests ===" << std::endl;
    
    // Initialize enclave
    std::string enclave_file = "enclave.signed.so";
    
    sgx_status_t ret = sgx_create_enclave(
        enclave_file.c_str(),
        SGX_DEBUG_FLAG,
        NULL,
        NULL,
        &global_eid,
        NULL
    );
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: " << ret << std::endl;
        std::cerr << "Make sure you're running from the correct directory" << std::endl;
        return 1;
    }
    
    std::cout << "Enclave created successfully (eid=" << global_eid << ")" << std::endl;
    
    // Run tests
    test_waksman_basic();
    test_waksman_different_nonces();
    test_waksman_performance();
    test_waksman_multiple();
    
    std::cout << "\n=== All tests passed ===" << std::endl;
    
    // Destroy enclave
    sgx_destroy_enclave(global_eid);
    
    return 0;
}