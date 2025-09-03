/**
 * Test to verify Waksman shuffle produces valid and uniform permutations
 * Decrypts after shuffle to check actual positions
 */

#include <iostream>
#include <vector>
#include <cstring>
#include <map>
#include <set>
#include <iomanip>
#include <algorithm>
#include <cmath>

#include "common/enclave_types.h"
#include "common/batch_types.h"
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
        e.attributes[0] = i;  // Store original position
        e.attributes[1] = i * 100;  // Some other value
        e.is_encrypted = 0;
        e.nonce = 0;
        entries.push_back(e);
    }
    
    return entries;
}

/**
 * Test distribution of shuffle results
 */
void test_distribution(size_t n, int num_trials = 1000) {
    std::cout << "\n=== Testing n=" << n << " with " << num_trials << " trials ===" << std::endl;
    
    // Track where each element ends up
    // position_counts[original_pos][final_pos] = count
    std::map<int, std::map<int, int>> position_counts;
    
    // Initialize counts
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            position_counts[i][j] = 0;
        }
    }
    
    // Track unique permutations seen
    std::set<std::vector<int>> unique_permutations;
    
    // Run trials
    for (int trial = 0; trial < num_trials; trial++) {
        // Create test data
        std::vector<entry_t> entries = create_test_entries(n);
        
        // Encrypt entries
        for (size_t i = 0; i < n; i++) {
            crypto_status_t enc_status;
            sgx_status_t ecall_ret = ecall_encrypt_entry(global_eid, &enc_status, &entries[i]);
            if (ecall_ret != SGX_SUCCESS || enc_status != CRYPTO_SUCCESS) {
                std::cerr << "Failed to encrypt entry " << i << std::endl;
                return;
            }
        }
        
        // Apply Waksman shuffle
        sgx_status_t status = SGX_SUCCESS;
        sgx_status_t ecall_status = ecall_oblivious_2way_waksman(
            global_eid, &status, entries.data(), n);
        
        if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
            std::cerr << "Shuffle failed on trial " << trial << std::endl;
            return;
        }
        
        // Decrypt to see actual positions
        for (size_t i = 0; i < n; i++) {
            crypto_status_t dec_status;
            sgx_status_t ecall_ret = ecall_decrypt_entry(global_eid, &dec_status, &entries[i]);
            if (ecall_ret != SGX_SUCCESS || dec_status != CRYPTO_SUCCESS) {
                std::cerr << "Failed to decrypt entry " << i << std::endl;
                return;
            }
        }
        
        // Record the permutation
        std::vector<int> permutation;
        for (size_t i = 0; i < n; i++) {
            int original_pos = entries[i].attributes[0];
            position_counts[original_pos][i]++;
            permutation.push_back(original_pos);
        }
        
        // Check if it's a valid permutation
        std::vector<int> sorted_perm = permutation;
        std::sort(sorted_perm.begin(), sorted_perm.end());
        bool is_valid = true;
        for (size_t i = 0; i < n; i++) {
            if (sorted_perm[i] != (int)i) {
                is_valid = false;
                break;
            }
        }
        
        if (!is_valid) {
            std::cerr << "INVALID PERMUTATION on trial " << trial << ": ";
            for (int val : permutation) {
                std::cerr << val << " ";
            }
            std::cerr << std::endl;
            return;
        }
        
        unique_permutations.insert(permutation);
    }
    
    // Analyze results
    std::cout << "Valid permutations: ALL " << num_trials << " trials produced valid permutations" << std::endl;
    std::cout << "Unique permutations seen: " << unique_permutations.size() 
              << " out of " << num_trials << " trials" << std::endl;
    
    // Check uniformity - each element should appear in each position roughly equally
    std::cout << "\nPosition distribution (row=original, col=final):" << std::endl;
    std::cout << "     ";
    for (size_t j = 0; j < n; j++) {
        std::cout << std::setw(6) << j;
    }
    std::cout << "  | Total" << std::endl;
    std::cout << "-----";
    for (size_t j = 0; j < n; j++) {
        std::cout << "------";
    }
    std::cout << "--|------" << std::endl;
    
    double expected_count = (double)num_trials / n;
    double max_deviation = 0.0;
    
    for (size_t i = 0; i < n; i++) {
        std::cout << std::setw(3) << i << ": ";
        int row_total = 0;
        for (size_t j = 0; j < n; j++) {
            int count = position_counts[i][j];
            row_total += count;
            std::cout << std::setw(6) << count;
            
            double deviation = std::abs(count - expected_count) / expected_count;
            max_deviation = std::max(max_deviation, deviation);
        }
        std::cout << "  | " << std::setw(5) << row_total << std::endl;
    }
    
    // Column totals
    std::cout << "-----";
    for (size_t j = 0; j < n; j++) {
        std::cout << "------";
    }
    std::cout << "--|------" << std::endl;
    std::cout << "Tot: ";
    for (size_t j = 0; j < n; j++) {
        int col_total = 0;
        for (size_t i = 0; i < n; i++) {
            col_total += position_counts[i][j];
        }
        std::cout << std::setw(6) << col_total;
    }
    std::cout << "  | " << std::setw(5) << (num_trials * n) << std::endl;
    
    std::cout << "\nExpected count per position: " << expected_count << std::endl;
    std::cout << "Maximum deviation from expected: " << (max_deviation * 100) << "%" << std::endl;
    
    // Check if distribution is reasonably uniform (within statistical bounds)
    // For 1000 trials, we expect sqrt(n*p*(1-p)) = sqrt(1000/n * (1-1/n)) ≈ sqrt(1000/n)
    double std_dev = std::sqrt(expected_count * (1.0 - 1.0/n));
    double three_sigma = 3 * std_dev;
    std::cout << "3-sigma range: [" << (expected_count - three_sigma) 
              << ", " << (expected_count + three_sigma) << "]" << std::endl;
    
    bool is_uniform = true;
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            int count = position_counts[i][j];
            if (count < expected_count - three_sigma || count > expected_count + three_sigma) {
                is_uniform = false;
                std::cout << "WARNING: Position [" << i << "][" << j << "] = " << count 
                          << " is outside 3-sigma range!" << std::endl;
            }
        }
    }
    
    if (is_uniform) {
        std::cout << "✓ Distribution appears uniform (all within 3-sigma)" << std::endl;
    } else {
        std::cout << "✗ Distribution may not be uniform" << std::endl;
    }
}

int main(int, char*[]) {
    std::cout << "=== Waksman Shuffle Distribution Test ===" << std::endl;
    
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
        return 1;
    }
    
    std::cout << "Enclave created successfully (eid=" << global_eid << ")" << std::endl;
    
    // Test power-of-2 sizes only (Waksman now requires power-of-2)
    test_distribution(2, 1000);
    test_distribution(4, 1000);
    test_distribution(8, 1000);
    test_distribution(16, 1000);
    test_distribution(32, 500);  // Fewer trials for larger sizes
    
    std::cout << "\n=== All tests completed ===" << std::endl;
    
    // Destroy enclave
    sgx_destroy_enclave(global_eid);
    
    return 0;
}