/**
 * Test to break down crypto and operation overhead in SGX
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <random>
#include <iomanip>
#include "../app/data_structures/entry.h"
#include "../app/crypto/crypto_utils.h"
#include "../app/Enclave_u.h"
#include "sgx_urts.h"

using namespace std::chrono;

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

// Initialize SGX enclave
bool initialize_enclave() {
    sgx_status_t ret = sgx_create_enclave("../enclave.signed.so", SGX_DEBUG_FLAG, 
                                         nullptr, nullptr, &global_eid, nullptr);
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error: " << ret << std::endl;
        return false;
    }
    
    std::cout << "Enclave initialized successfully" << std::endl;
    return true;
}

// Test crypto and operation breakdown
void test_crypto_operation_breakdown() {
    std::cout << "\n=== Crypto and Operation Breakdown ===" << std::endl;
    
    std::vector<size_t> test_sizes = {100, 500, 1000, 2000};
    const int iterations = 100;
    
    for (size_t count : test_sizes) {
        std::cout << "\n--- Testing with " << count << " entries ---" << std::endl;
        
        // Create encrypted test entries
        std::vector<Entry> entries(count);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 1000000);
        
        for (size_t i = 0; i < count; i++) {
            entries[i] = Entry();  // Properly initialize with default constructor
            entries[i].join_attr = dis(gen);
            entries[i].original_index = i;
            entries[i].is_encrypted = false;
            // Initialize some attributes with test data
            for (int j = 0; j < 10 && j < MAX_ATTRIBUTES; j++) {
                entries[i].attributes[j] = dis(gen);
            }
            // Encrypt outside enclave
            CryptoUtils::encrypt_entry(entries[i], global_eid);
        }
        
        // Convert to entry_t
        std::vector<entry_t> c_entries;
        c_entries.reserve(count);
        for (const auto& entry : entries) {
            c_entries.push_back(entry.to_entry_t());
        }
        
        // Test 1: Decrypt only
        auto test_entries = c_entries;  // Copy for each test
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            test_entries = c_entries;  // Reset to encrypted
            ecall_test_decrypt_only(global_eid, test_entries.data(), count);
        }
        auto end = high_resolution_clock::now();
        auto decrypt_time = duration_cast<microseconds>(end - start).count();
        
        // Test 2: Encrypt only (use decrypted entries)
        test_entries = c_entries;
        ecall_test_decrypt_only(global_eid, test_entries.data(), count);  // Decrypt first
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            auto temp = test_entries;  // Copy decrypted entries
            ecall_test_encrypt_only(global_eid, temp.data(), count);
        }
        end = high_resolution_clock::now();
        auto encrypt_time = duration_cast<microseconds>(end - start).count();
        
        // Test 3: Compare only (on plaintext)
        test_entries = c_entries;
        for (auto& e : test_entries) {
            e.is_encrypted = 0;  // Mark as plaintext
        }
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            ecall_test_compare_only(global_eid, test_entries.data(), count);
        }
        end = high_resolution_clock::now();
        auto compare_time = duration_cast<microseconds>(end - start).count();
        
        // Test 4: Decrypt + Compare (no re-encrypt)
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            test_entries = c_entries;  // Reset to encrypted
            ecall_test_decrypt_and_compare(global_eid, test_entries.data(), count);
        }
        end = high_resolution_clock::now();
        auto decrypt_compare_time = duration_cast<microseconds>(end - start).count();
        
        // Test 5: Full cycle (decrypt + compare + encrypt)
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            test_entries = c_entries;  // Reset to encrypted
            ecall_test_full_cycle(global_eid, test_entries.data(), count);
        }
        end = high_resolution_clock::now();
        auto full_cycle_time = duration_cast<microseconds>(end - start).count();
        
        // Calculate per-iteration times
        double decrypt_per_iter = (double)decrypt_time / iterations;
        double encrypt_per_iter = (double)encrypt_time / iterations;
        double compare_per_iter = (double)compare_time / iterations;
        double decrypt_compare_per_iter = (double)decrypt_compare_time / iterations;
        double full_cycle_per_iter = (double)full_cycle_time / iterations;
        
        // Calculate per-entry times
        double decrypt_per_entry = decrypt_per_iter / count;
        double encrypt_per_entry = encrypt_per_iter / count;
        double compare_per_op = compare_per_iter / (count / 2);  // Operations are on pairs
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Decrypt only: " << decrypt_per_iter << " μs (" 
                  << decrypt_per_entry << " μs/entry)" << std::endl;
        std::cout << "  Encrypt only: " << encrypt_per_iter << " μs (" 
                  << encrypt_per_entry << " μs/entry)" << std::endl;
        std::cout << "  Compare only: " << compare_per_iter << " μs (" 
                  << compare_per_op << " μs/operation)" << std::endl;
        std::cout << "  Decrypt + Compare: " << decrypt_compare_per_iter << " μs" << std::endl;
        std::cout << "  Full cycle: " << full_cycle_per_iter << " μs" << std::endl;
        
        std::cout << "\nBreakdown:" << std::endl;
        std::cout << "  Decryption: " << (decrypt_per_iter / full_cycle_per_iter * 100) << "%" << std::endl;
        std::cout << "  Operations: " << (compare_per_iter / full_cycle_per_iter * 100) << "%" << std::endl;
        std::cout << "  Encryption: " << (encrypt_per_iter / full_cycle_per_iter * 100) << "%" << std::endl;
        
        // Verify additive property
        double sum = decrypt_per_iter + compare_per_iter + encrypt_per_iter;
        double overhead = full_cycle_per_iter - sum;
        std::cout << "  Overhead/other: " << overhead << " μs (" 
                  << (overhead / full_cycle_per_iter * 100) << "%)" << std::endl;
    }
}

// Test with batch operations (more realistic)
void test_batch_operation_breakdown() {
    std::cout << "\n=== Batch Operation Breakdown (2000 entries, 1000 ops) ===" << std::endl;
    
    const size_t num_entries = 2000;
    const size_t num_ops = 1000;
    const int iterations = 50;
    
    // Create encrypted entries
    std::vector<Entry> entries(num_entries);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000000);
    
    for (size_t i = 0; i < num_entries; i++) {
        entries[i] = Entry();  // Properly initialize with default constructor
        entries[i].join_attr = dis(gen);
        entries[i].original_index = i;
        entries[i].is_encrypted = false;
        // Initialize some attributes with test data
        for (int j = 0; j < 10 && j < MAX_ATTRIBUTES; j++) {
            entries[i].attributes[j] = dis(gen);
        }
        CryptoUtils::encrypt_entry(entries[i], global_eid);
    }
    
    // Convert to entry_t
    std::vector<entry_t> c_entries;
    for (const auto& entry : entries) {
        c_entries.push_back(entry.to_entry_t());
    }
    
    // Create batch operations
    std::vector<BatchOperation> ops(num_ops);
    for (size_t i = 0; i < num_ops; i++) {
        ops[i].idx1 = i * 2;
        ops[i].idx2 = i * 2 + 1;
        for (int j = 0; j < MAX_EXTRA_PARAMS; j++) {
            ops[i].extra_params[j] = BATCH_NO_PARAM;
        }
    }
    
    // Test actual batch dispatcher
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        auto test_entries = c_entries;
        sgx_status_t status = ecall_batch_dispatcher(
            global_eid,
            test_entries.data(),
            test_entries.size(),
            ops.data(),
            ops.size(),
            ops.size() * sizeof(BatchOperation),
            OP_ECALL_COMPARATOR_JOIN_ATTR
        );
        if (status != SGX_SUCCESS) {
            std::cerr << "Batch dispatcher failed" << std::endl;
            return;
        }
    }
    auto end = high_resolution_clock::now();
    auto batch_time = duration_cast<microseconds>(end - start).count();
    
    // Test components separately
    auto test_entries = c_entries;
    
    // Decrypt time
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        test_entries = c_entries;
        ecall_test_decrypt_only(global_eid, test_entries.data(), num_entries);
    }
    end = high_resolution_clock::now();
    auto decrypt_time = duration_cast<microseconds>(end - start).count();
    
    // Encrypt time
    test_entries = c_entries;
    ecall_test_decrypt_only(global_eid, test_entries.data(), num_entries);
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        auto temp = test_entries;
        ecall_test_encrypt_only(global_eid, temp.data(), num_entries);
    }
    end = high_resolution_clock::now();
    auto encrypt_time = duration_cast<microseconds>(end - start).count();
    
    // Compare time (1000 operations)
    test_entries = c_entries;
    for (auto& e : test_entries) {
        e.is_encrypted = 0;
    }
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        ecall_test_compare_only(global_eid, test_entries.data(), num_entries);
    }
    end = high_resolution_clock::now();
    auto compare_time = duration_cast<microseconds>(end - start).count();
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Batch dispatcher total: " << (double)batch_time/iterations << " μs/call" << std::endl;
    std::cout << "\nComponent breakdown:" << std::endl;
    std::cout << "  Decrypt 2000 entries: " << (double)decrypt_time/iterations << " μs" << std::endl;
    std::cout << "  1000 comparisons: " << (double)compare_time/iterations << " μs" << std::endl;
    std::cout << "  Encrypt 2000 entries: " << (double)encrypt_time/iterations << " μs" << std::endl;
    
    double total_components = (double)(decrypt_time + compare_time + encrypt_time)/iterations;
    double batch_dispatcher_overhead = (double)batch_time/iterations - total_components;
    
    std::cout << "\nPercentage breakdown:" << std::endl;
    std::cout << "  Decryption: " << ((double)decrypt_time/batch_time * 100) << "%" << std::endl;
    std::cout << "  Operations: " << ((double)compare_time/batch_time * 100) << "%" << std::endl;
    std::cout << "  Encryption: " << ((double)encrypt_time/batch_time * 100) << "%" << std::endl;
    std::cout << "  Switch dispatch/other: " << (batch_dispatcher_overhead/(batch_time/iterations) * 100) << "%" << std::endl;
    
    std::cout << "\nPer-unit costs:" << std::endl;
    std::cout << "  Per entry decrypt: " << (double)decrypt_time/(iterations * num_entries) << " μs" << std::endl;
    std::cout << "  Per operation: " << (double)compare_time/(iterations * num_ops) << " μs" << std::endl;
    std::cout << "  Per entry encrypt: " << (double)encrypt_time/(iterations * num_entries) << " μs" << std::endl;
}

int main() {
    std::cout << "SGX Crypto and Operation Breakdown Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Initialize enclave
    if (!initialize_enclave()) {
        return 1;
    }
    
    // Run tests
    test_crypto_operation_breakdown();
    test_batch_operation_breakdown();
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    
    return 0;
}