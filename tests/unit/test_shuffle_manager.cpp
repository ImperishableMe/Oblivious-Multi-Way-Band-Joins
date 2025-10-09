/**
 * Test for ShuffleManager - tests both small and large vector shuffling
 */

#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <set>
#include <cmath>

#include "../../app/data_structures/table.h"
#include "../../app/algorithms/shuffle_manager.h"
#include "../../app/crypto/crypto_utils.h"
#include "../../common/enclave_types.h"
#include "../../common/constants.h"
#include "sgx_compat/Enclave_u.h"
#include "sgx_compat/sgx_urts.h"

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

// Helper to calculate expected padding and recursion depth
size_t calculate_expected_padding(size_t n, size_t& out_b) {
    const size_t batch_size = 2000;  // MAX_BATCH_SIZE
    const size_t k = 8;  // MERGE_SORT_K
    
    if (n <= batch_size) {
        out_b = 0;
        size_t power = 1;
        while (power < n) power *= 2;
        return power;
    }
    
    // Calculate b
    size_t temp = n;
    size_t b = 0;
    size_t k_power = 1;
    while (temp > batch_size) {
        temp = (temp + k - 1) / k;
        b++;
        k_power *= k;
    }
    
    // Get power of 2 for final level
    size_t a_part = 1;
    while (a_part < temp) a_part *= 2;
    
    out_b = b;
    return a_part * k_power;
}

/**
 * Create a test table with sequential values
 */
Table create_test_table(size_t n, const std::string& name, sgx_enclave_id_t eid) {
    std::vector<std::string> schema = {"id", "value"};
    Table table(name, schema);
    
    for (size_t i = 0; i < n; i++) {
        Entry e;
        e.attributes[0] = i;           // Sequential ID
        e.attributes[1] = i * 100;      // Some value
        e.field_type = SOURCE;
        e.is_encrypted = 0;
        
        // Encrypt the entry
        crypto_status_t enc_status;
        entry_t entry_c = e.to_entry_t();
        sgx_status_t ecall_ret = ecall_encrypt_entry(eid, &enc_status, &entry_c);
        if (ecall_ret != SGX_SUCCESS || enc_status != CRYPTO_SUCCESS) {
            std::cerr << "Failed to encrypt entry " << i << std::endl;
            continue;
        }
        e.from_entry_t(entry_c);
        
        table.add_entry(e);
    }
    
    return table;
}

/**
 * Test small vector shuffle (uses 2-way Waksman directly)
 */
void test_small_shuffle() {
    std::cout << "\n=== Testing Small Vector Shuffle (n < MAX_BATCH_SIZE) ===" << std::endl;
    
    std::vector<size_t> test_sizes = {10, 50, 100, 500, 1000, 1500};
    
    for (size_t n : test_sizes) {
        size_t recursion_depth;
        size_t expected_padded = calculate_expected_padding(n, recursion_depth);
        std::cout << "\nTesting n=" << n << " (padded to " << expected_padded << ")" << std::endl;
        
        // Create test table with encrypted entries
        Table table = create_test_table(n, "test_small", global_eid);
        size_t original_size = table.size();
        
        // Pad table to shuffle size before shuffling
        table.pad_to_shuffle_size(global_eid);
        
        // Apply shuffle using ShuffleManager directly
        ShuffleManager shuffle_mgr(global_eid);
        
        auto start = std::chrono::high_resolution_clock::now();
        shuffle_mgr.shuffle(table);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Decrypt all entries and filter out padding
        std::vector<Entry> real_entries;
        int padding_count = 0;
        int real_count = 0;
        for (size_t i = 0; i < table.size(); i++) {
            Entry e = table[i];
            if (e.is_encrypted) {
                crypto_status_t dec_status;
                entry_t entry_c = e.to_entry_t();
                sgx_status_t ecall_ret = ecall_decrypt_entry(global_eid, &dec_status, &entry_c);
                if (ecall_ret != SGX_SUCCESS || dec_status != CRYPTO_SUCCESS) {
                    std::cerr << "Failed to decrypt entry " << i << std::endl;
                    continue;
                }
                e.from_entry_t(entry_c);
            }
            
            // Filter out SORT_PADDING entries
            if (e.field_type != SORT_PADDING) {
                real_entries.push_back(e);
                real_count++;
            } else {
                padding_count++;
            }
        }
        
        // Verify padding is correct
        if (table.size() != expected_padded) {
            std::cerr << "ERROR: Expected padded size " << expected_padded 
                     << " but got " << table.size() << std::endl;
            continue;
        }
        
        // After shuffle, table should have padded size
        // Real entries should equal original size after filtering
        if (real_entries.size() != original_size) {
            std::cerr << "ERROR: Real entries count changed from " << original_size 
                     << " to " << real_entries.size() 
                     << " (total with padding: " << table.size() << ")" << std::endl;
            continue;
        }
        
        // Check that all original values are still present (valid permutation)
        std::set<int32_t> original_ids;
        std::set<int32_t> shuffled_ids;
        
        for (size_t i = 0; i < n; i++) {
            original_ids.insert(i);
        }
        
        for (const auto& e : real_entries) {
            shuffled_ids.insert(e.attributes[0]);
        }
        
        if (original_ids != shuffled_ids) {
            std::cerr << "ERROR: Not a valid permutation!" << std::endl;
            continue;
        }
        
        // Check that order changed (not identical)
        bool order_changed = false;
        for (size_t i = 0; i < real_entries.size(); i++) {
            if (real_entries[i].attributes[0] != (int32_t)i) {
                order_changed = true;
                break;
            }
        }
        
        std::cout << "  Size preserved: ✓" << std::endl;
        std::cout << "  Valid permutation: ✓" << std::endl;
        std::cout << "  Order changed: " << (order_changed ? "✓" : "✗ (might be identity)") << std::endl;
        std::cout << "  Time: " << duration.count() << " μs (" 
                  << duration.count() / n << " μs/element)" << std::endl;
    }
}

/**
 * Test large vector shuffle (uses k-way recursive decomposition)
 */
void test_large_shuffle() {
    std::cout << "\n=== Testing Large Vector Shuffle (n > MAX_BATCH_SIZE) ===" << std::endl;
    
    // Test sizes larger than MAX_BATCH_SIZE (2000)
    // b=1: up to 16000, b=2: up to 128000
    std::vector<size_t> test_sizes = {2100, 4096, 8192, 17000, 32768};
    
    for (size_t n : test_sizes) {
        size_t recursion_depth;
        size_t expected_padded = calculate_expected_padding(n, recursion_depth);
        std::cout << "\nTesting n=" << n << " (recursion depth b=" << recursion_depth 
                  << ", padded to " << expected_padded << ")" << std::endl;
        
        // Create test table with encrypted entries
        Table table = create_test_table(n, "test_large", global_eid);
        size_t original_size = table.size();
        
        // Pad table to shuffle size before shuffling
        table.pad_to_shuffle_size(global_eid);
        
        // Apply shuffle using ShuffleManager
        ShuffleManager shuffle_mgr(global_eid);
        
        auto start = std::chrono::high_resolution_clock::now();
        shuffle_mgr.shuffle(table);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Decrypt all entries and filter out padding
        std::vector<Entry> real_entries;
        int padding_count = 0;
        int real_count = 0;
        for (size_t i = 0; i < table.size(); i++) {
            Entry e = table[i];
            if (e.is_encrypted) {
                crypto_status_t dec_status;
                entry_t entry_c = e.to_entry_t();
                sgx_status_t ecall_ret = ecall_decrypt_entry(global_eid, &dec_status, &entry_c);
                if (ecall_ret != SGX_SUCCESS || dec_status != CRYPTO_SUCCESS) {
                    std::cerr << "Failed to decrypt entry " << i << std::endl;
                    continue;
                }
                e.from_entry_t(entry_c);
            }
            
            // Filter out SORT_PADDING entries
            if (e.field_type != SORT_PADDING) {
                real_entries.push_back(e);
                real_count++;
            } else {
                padding_count++;
            }
        }
        
        // Verify padding is correct
        if (table.size() != expected_padded) {
            std::cerr << "ERROR: Expected padded size " << expected_padded 
                     << " but got " << table.size() << std::endl;
            continue;
        }
        
        // After shuffle, table should have padded size
        // Real entries should equal original size after filtering
        if (real_entries.size() != original_size) {
            std::cerr << "ERROR: Real entries count changed from " << original_size 
                     << " to " << real_entries.size() 
                     << " (total with padding: " << table.size() << ")" << std::endl;
            continue;
        }
        
        // Check that all original values are still present
        std::set<int32_t> original_ids;
        std::set<int32_t> shuffled_ids;
        
        for (size_t i = 0; i < n; i++) {
            original_ids.insert(i);
        }
        
        for (const auto& e : real_entries) {
            shuffled_ids.insert(e.attributes[0]);
        }
        
        if (original_ids != shuffled_ids) {
            std::cerr << "ERROR: Not a valid permutation!" << std::endl;
            
            // Debug: Show what's missing/extra
            std::vector<int32_t> missing;
            std::vector<int32_t> extra;
            std::set_difference(original_ids.begin(), original_ids.end(),
                              shuffled_ids.begin(), shuffled_ids.end(),
                              std::back_inserter(missing));
            std::set_difference(shuffled_ids.begin(), shuffled_ids.end(),
                              original_ids.begin(), original_ids.end(),
                              std::back_inserter(extra));
            
            if (!missing.empty()) {
                std::cerr << "Missing IDs (first 20): ";
                for (size_t i = 0; i < std::min(missing.size(), (size_t)20); i++) {
                    std::cerr << missing[i] << " ";
                }
                if (missing.size() > 20) std::cerr << "... (" << missing.size() << " total)";
                std::cerr << std::endl;
            }
            if (!extra.empty()) {
                std::cerr << "Extra IDs (first 20): ";
                for (size_t i = 0; i < std::min(extra.size(), (size_t)20); i++) {
                    std::cerr << extra[i] << " ";
                }
                if (extra.size() > 20) std::cerr << "... (" << extra.size() << " total)";
                std::cerr << std::endl;
            }
            continue;
        }
        
        std::cout << "  Size preserved: ✓" << std::endl;
        std::cout << "  Valid permutation: ✓" << std::endl;
        std::cout << "  Time: " << duration.count() << " μs (" 
                  << duration.count() / n << " μs/element)" << std::endl;
    }
}

/**
 * Test shuffle randomness - verify that shuffles produce different permutations
 */
void test_shuffle_randomness() {
    std::cout << "\n=== Testing Shuffle Randomness ===" << std::endl;
    
    // Test 1: Quick uniqueness test with fewer trials
    size_t n = 100;  // Small size for easier analysis
    int num_trials = 10;
    
    std::cout << "\nTesting with n=" << n << ", " << num_trials << " trials" << std::endl;
    
    // Store permutations from multiple shuffles
    std::vector<std::vector<int32_t>> permutations;
    
    for (int trial = 0; trial < num_trials; trial++) {
        // Create identical starting table
        Table table = create_test_table(n, "test_random", global_eid);
        
        // Pad table to shuffle size before shuffling
        table.pad_to_shuffle_size(global_eid);
        
        // Apply shuffle
        ShuffleManager shuffle_mgr(global_eid);
        shuffle_mgr.shuffle(table);
        
        // Decrypt and extract permutation (filter padding)
        std::vector<int32_t> perm;
        for (size_t i = 0; i < table.size(); i++) {
            Entry e = table[i];
            if (e.is_encrypted) {
                crypto_status_t dec_status;
                entry_t entry_c = e.to_entry_t();
                ecall_decrypt_entry(global_eid, &dec_status, &entry_c);
                e.from_entry_t(entry_c);
            }
            if (e.field_type != SORT_PADDING) {
                perm.push_back(e.attributes[0]);
            }
        }
        
        permutations.push_back(perm);
    }
    
    // Check that permutations are different
    int identical_pairs = 0;
    for (size_t i = 0; i < permutations.size(); i++) {
        for (size_t j = i + 1; j < permutations.size(); j++) {
            if (permutations[i] == permutations[j]) {
                identical_pairs++;
            }
        }
    }
    
    std::cout << "  Unique permutations: " << (identical_pairs == 0 ? "✓ All different" : 
                  "✗ Found " + std::to_string(identical_pairs) + " identical pairs") << std::endl;
    
    // Statistical analysis: position distribution
    std::cout << "\n  Position distribution analysis (first 10 elements):" << std::endl;
    
    // For each original position, track where it ends up
    std::vector<std::map<int, int>> position_counts(std::min(10UL, n));
    
    for (const auto& perm : permutations) {
        for (size_t pos = 0; pos < std::min(10UL, perm.size()); pos++) {
            position_counts[pos][perm[pos]]++;
        }
    }
    
    // Show distribution for first few positions
    for (size_t pos = 0; pos < std::min(5UL, position_counts.size()); pos++) {
        std::cout << "    Position " << pos << " values: ";
        std::set<int> unique_values;
        for (const auto& kv : position_counts[pos]) {
            unique_values.insert(kv.first);
        }
        std::cout << unique_values.size() << " unique values out of " << num_trials << " trials" << std::endl;
    }
    
    // Chi-square test for uniformity (simplified)
    std::cout << "\n  Chi-square test for position 0:" << std::endl;
    double expected = (double)num_trials / n;  // Expected frequency for uniform distribution
    double chi_square = 0.0;
    int observed_values = 0;
    
    for (int val = 0; val < (int)n; val++) {
        int observed = position_counts[0][val];
        if (observed > 0) {
            observed_values++;
            double diff = observed - expected;
            chi_square += (diff * diff) / expected;
        }
    }
    
    std::cout << "    Observed " << observed_values << " different values at position 0" << std::endl;
    std::cout << "    Chi-square statistic: " << chi_square << std::endl;
    std::cout << "    (Lower is more uniform, expected ~" << n << " for random)" << std::endl;
    
    // Calculate entropy for position 0
    double entropy = 0.0;
    for (const auto& kv : position_counts[0]) {
        if (kv.second > 0) {
            double p = (double)kv.second / num_trials;
            entropy -= p * log2(p);
        }
    }
    double max_entropy = log2(std::min((size_t)num_trials, n));  // Maximum possible entropy
    
    std::cout << "\n  Entropy analysis:" << std::endl;
    std::cout << "    Position 0 entropy: " << entropy << " bits" << std::endl;
    std::cout << "    Maximum entropy: " << max_entropy << " bits" << std::endl;
    std::cout << "    Randomness quality: " << (entropy / max_entropy * 100) << "%" << std::endl;
}

/**
 * Test shuffle_merge_sort integration
 */
void test_shuffle_merge_sort() {
    std::cout << "\n=== Testing shuffle_merge_sort Integration ===" << std::endl;
    
    // Test different recursion depths: b=0 (100, 1000), b=1 (2500, 5000), b=2 (20000)
    std::vector<size_t> test_sizes = {100, 1000, 2500, 5000, 20000};
    
    for (size_t n : test_sizes) {
        size_t recursion_depth;
        size_t expected_padded = calculate_expected_padding(n, recursion_depth);
        std::cout << "\nTesting shuffle_merge_sort with n=" << n 
                  << " (recursion depth b=" << recursion_depth << ")" << std::endl;
        
        // Create test table with random values (encrypted)
        std::vector<std::string> schema = {"id", "value"};
        Table table("test_sort", schema);
        
        for (size_t i = 0; i < n; i++) {
            Entry e;
            e.attributes[0] = rand() % 10000;  // Random values to sort
            e.attributes[1] = i;               // Original position
            e.field_type = SOURCE;
            e.join_attr = e.attributes[0];     // Sort by first attribute
            e.is_encrypted = 0;
            
            // Encrypt the entry
            crypto_status_t enc_status;
            entry_t entry_c = e.to_entry_t();
            sgx_status_t ecall_ret = ecall_encrypt_entry(global_eid, &enc_status, &entry_c);
            if (ecall_ret != SGX_SUCCESS || enc_status != CRYPTO_SUCCESS) {
                std::cerr << "Failed to encrypt entry " << i << std::endl;
                continue;
            }
            e.from_entry_t(entry_c);
            
            table.add_entry(e);
        }
        
        // Apply shuffle_merge_sort
        auto start = std::chrono::high_resolution_clock::now();
        table.shuffle_merge_sort(global_eid, OP_ECALL_COMPARATOR_JOIN_THEN_OTHER);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Decrypt and verify sorted
        bool is_sorted = true;
        std::vector<int32_t> sorted_values;
        
        for (size_t i = 0; i < table.size(); i++) {
            Entry e = table[i];
            if (e.is_encrypted) {
                crypto_status_t dec_status;
                entry_t entry_c = e.to_entry_t();
                sgx_status_t ecall_ret = ecall_decrypt_entry(global_eid, &dec_status, &entry_c);
                if (ecall_ret != SGX_SUCCESS || dec_status != CRYPTO_SUCCESS) {
                    std::cerr << "Failed to decrypt entry " << i << std::endl;
                    is_sorted = false;
                    break;
                }
                e.from_entry_t(entry_c);
            }
            sorted_values.push_back(e.join_attr);
        }
        
        for (size_t i = 1; i < sorted_values.size(); i++) {
            if (sorted_values[i-1] > sorted_values[i]) {
                is_sorted = false;
                std::cerr << "Not sorted at position " << i << ": " 
                         << sorted_values[i-1] << " > " << sorted_values[i] << std::endl;
                break;
            }
        }
        
        std::cout << "  Size preserved: " << (table.size() == n ? "✓" : "✗") << std::endl;
        std::cout << "  Correctly sorted: " << (is_sorted ? "✓" : "✗") << std::endl;
        std::cout << "  Time: " << duration.count() << " μs (" 
                  << duration.count() / n << " μs/element)" << std::endl;
    }
}

int main(int, char*[]) {
    std::cout << "=== ShuffleManager Test Suite ===" << std::endl;
    
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
    
    // Run tests
    test_small_shuffle();
    test_large_shuffle();
    test_shuffle_randomness();
    test_shuffle_merge_sort();
    
    std::cout << "\n=== All tests completed ===" << std::endl;
    
    // Destroy enclave
    sgx_destroy_enclave(global_eid);
    
    return 0;
}