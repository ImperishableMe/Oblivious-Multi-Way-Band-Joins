#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include "app/data_structures/data_structures.h"
#include "app/crypto/crypto_utils.h"
#include "sgx_compat/Enclave_u.h"
#include "sgx_compat/sgx_urts.h"
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

// Test merge sort vs std::sort
void test_sort_comparison(size_t table_size) {
    std::cout << "\n=== Testing sort with " << table_size << " entries ===" << std::endl;
    
    // Create two identical tables
    Table table1 = create_random_table(table_size, "test_table1");
    Table table2("test_table2", table1.get_schema());
    
    // Copy entries to table2
    for (size_t i = 0; i < table1.size(); i++) {
        table2.add_entry(table1[i]);
    }
    
    // Store original size for verification
    size_t original_size = table1.size();
    
    // Test std::sort (as reference)
    auto start = std::chrono::high_resolution_clock::now();
    std::sort(table1.begin(), table1.end(), [](const Entry& a, const Entry& b) {
        return a.join_attr < b.join_attr;
    });
    auto end = std::chrono::high_resolution_clock::now();
    auto std_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Encrypt table2 for merge sort test
    for (size_t i = 0; i < table2.size(); i++) {
        CryptoUtils::encrypt_entry(table2[i], global_eid);
    }
    
    // Test merge sort
    start = std::chrono::high_resolution_clock::now();
    table2.shuffle_merge_sort(global_eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    end = std::chrono::high_resolution_clock::now();
    auto merge_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Decrypt for verification
    for (size_t i = 0; i < table2.size(); i++) {
        CryptoUtils::decrypt_entry(table2[i], global_eid);
    }
    
    // Check if sizes changed
    bool size_preserved = (table1.size() == original_size) && (table2.size() == original_size);
    if (!size_preserved) {
        std::cout << "ERROR: Size changed! Original=" << original_size 
                  << ", std::sort=" << table1.size() 
                  << ", merge_sort=" << table2.size() << std::endl;
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
        if (table1[i-1].join_attr > table1[i].join_attr) {
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
    std::cout << "  Original size: " << original_size << std::endl;
    std::cout << "  std::sort:     " << std_time << "ms, size=" << table1.size() 
              << ", sorted=" << (sorted1 ? "YES" : "NO") << std::endl;
    std::cout << "  Merge sort:    " << merge_time << "ms, size=" << table2.size()
              << ", sorted=" << (sorted2 ? "YES" : "NO") << std::endl;
    std::cout << "  Match: " << (match ? "YES" : "NO") << std::endl;
    std::cout << "  Size preserved: " << (size_preserved ? "YES" : "NO") << std::endl;
}

// Special test for exactly 29,929 rows (the problematic case)
void test_exact_29929_rows() {
    std::cout << "\n=== SPECIAL TEST: Exactly 29,929 rows ===" << std::endl;
    
    // Create table with exactly 29,929 rows
    Table table_std = create_random_table(29929, "test_29929_std");
    Table table_merge("test_29929_merge", table_std.get_schema());
    
    // Copy entries
    for (size_t i = 0; i < table_std.size(); i++) {
        table_merge.add_entry(table_std[i]);
    }
    
    std::cout << "Created tables with " << table_std.size() << " rows" << std::endl;
    
    // Test std::sort
    std::sort(table_std.begin(), table_std.end(), [](const Entry& a, const Entry& b) {
        return a.join_attr < b.join_attr;
    });
    std::cout << "After std::sort: " << table_std.size() << " rows" << std::endl;
    
    // Encrypt and test merge sort
    for (size_t i = 0; i < table_merge.size(); i++) {
        CryptoUtils::encrypt_entry(table_merge[i], global_eid);
    }
    
    table_merge.shuffle_merge_sort(global_eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    
    // Decrypt
    for (size_t i = 0; i < table_merge.size(); i++) {
        CryptoUtils::decrypt_entry(table_merge[i], global_eid);
    }
    
    std::cout << "After merge sort: " << table_merge.size() << " rows" << std::endl;
    
    if (table_merge.size() != 29929) {
        std::cout << "*** ERROR: Merge sort changed row count from 29929 to " 
                  << table_merge.size() << " ***" << std::endl;
        std::cout << "*** This explains the tm2 test failure! ***" << std::endl;
    } else {
        std::cout << "Row count preserved correctly." << std::endl;
    }
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
    
    // Test around the problematic size to find pattern
    test_sort_comparison(29900);  // Just below
    test_sort_comparison(29929);  // The exact problematic size
    test_sort_comparison(30000);  // Round number
    test_sort_comparison(30100);  // Just above
    
    // Special test for the problematic size
    test_exact_29929_rows();
    
    // Cleanup
    sgx_destroy_enclave(global_eid);
    
    std::cout << "\nAll tests complete!" << std::endl;
    return 0;
}