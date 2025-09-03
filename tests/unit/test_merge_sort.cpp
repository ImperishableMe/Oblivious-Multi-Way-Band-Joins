#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include "app/data_structures/data_structures.h"
#include "app/crypto/crypto_utils.h"
#include "enclave/untrusted/Enclave_u.h"
#include "sgx_urts.h"
#include "common/batch_types.h"
#include "common/debug_util.h"

sgx_enclave_id_t global_eid = 0;

// Initialize enclave
int initialize_enclave() {
    sgx_status_t ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG,
                                          NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return -1;
    }
    return 0;
}

// Create a table with random data
Table create_random_table(size_t size, const std::string& name) {
    std::vector<std::string> schema = {"col1", "col2", "col3"};
    Table table(name, schema);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000);
    
    for (size_t i = 0; i < size; i++) {
        Entry entry;
        entry.join_attr = dis(gen);
        entry.original_index = i;
        entry.field_type = SOURCE;
        entry.equality_type = EQ;
        
        for (int j = 0; j < 3; j++) {
            entry.attributes[j] = dis(gen);
        }
        
        table.add_entry(entry);
    }
    
    return table;
}

// Test heap sort vs bitonic sort
void test_sort_comparison(size_t table_size) {
    std::cout << "\n=== Testing sort with " << table_size << " entries ===" << std::endl;
    
    // Create two identical tables
    Table table1 = create_random_table(table_size, "test_table1");
    Table table2("test_table2", table1.get_schema());
    
    // Copy entries to table2
    for (size_t i = 0; i < table1.size(); i++) {
        table2.add_entry(table1[i]);
    }
    
    // Encrypt tables
    for (size_t i = 0; i < table1.size(); i++) {
        CryptoUtils::encrypt_entry(table1[i], global_eid);
    }
    for (size_t i = 0; i < table2.size(); i++) {
        CryptoUtils::encrypt_entry(table2[i], global_eid);
    }
    
    // Test bitonic sort
    auto start = std::chrono::high_resolution_clock::now();
    table1.batched_oblivious_sort(global_eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    auto end = std::chrono::high_resolution_clock::now();
    auto bitonic_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Test merge sort
    start = std::chrono::high_resolution_clock::now();
    table2.non_oblivious_merge_sort(global_eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    end = std::chrono::high_resolution_clock::now();
    auto merge_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Decrypt for verification
    for (size_t i = 0; i < table1.size(); i++) {
        CryptoUtils::decrypt_entry(table1[i], global_eid);
    }
    for (size_t i = 0; i < table2.size(); i++) {
        CryptoUtils::decrypt_entry(table2[i], global_eid);
    }
    
    // Verify both sorts produce same result
    bool match = true;
    if (table1.size() != table2.size()) {
        match = false;
        std::cout << "Size mismatch: " << table1.size() << " vs " << table2.size() << std::endl;
    } else {
        for (size_t i = 0; i < table1.size(); i++) {
            if (table1[i].join_attr != table2[i].join_attr) {
                match = false;
                std::cout << "Mismatch at index " << i << ": " 
                         << table1[i].join_attr << " vs " << table2[i].join_attr << std::endl;
                break;
            }
        }
    }
    
    // Verify sorted order
    bool sorted1 = true, sorted2 = true;
    for (size_t i = 1; i < table1.size(); i++) {
        if (table1[i-1].join_attr > table1[i].join_attr && 
            table1[i].field_type != SORT_PADDING) {
            sorted1 = false;
            break;
        }
    }
    for (size_t i = 1; i < table2.size(); i++) {
        if (table2[i-1].join_attr > table2[i].join_attr && 
            table2[i].field_type != SORT_PADDING) {
            sorted2 = false;
            break;
        }
    }
    
    std::cout << "Results:" << std::endl;
    std::cout << "  Bitonic sort: " << bitonic_time << "ms, sorted=" << (sorted1 ? "YES" : "NO") << std::endl;
    std::cout << "  Merge sort:   " << merge_time << "ms, sorted=" << (sorted2 ? "YES" : "NO") << std::endl;
    std::cout << "  Match: " << (match ? "YES" : "NO") << std::endl;
    std::cout << "  Speedup: " << (double)bitonic_time / merge_time << "x" << std::endl;
}

int main(int argc, char* argv[]) {
    // Initialize enclave
    if (initialize_enclave() < 0) {
        return 1;
    }
    
    std::cout << "Testing Non-Oblivious Merge Sort\n";
    std::cout << "=================================\n";
    
    // Test with different sizes
    test_sort_comparison(10);     // Small
    test_sort_comparison(100);    // Medium
    test_sort_comparison(1000);   // Large
    test_sort_comparison(5000);   // Very large
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    
    std::cout << "\nAll tests complete!" << std::endl;
    return 0;
}