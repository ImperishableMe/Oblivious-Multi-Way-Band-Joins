#include <iostream>
#include <cstring>
#include <cassert>
#include <vector>
#include <random>
#include <limits>
#include "sgx_compat/sgx_urts.h"
#include "sgx_compat/Enclave_u.h"
#include "../../src/core/entry.h"
#include "../../src/crypto/crypto_utils.h"

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

void report_test_result(const std::string& test_name, bool passed) {
    if (passed) {
        std::cout << "[PASS] " << test_name << std::endl;
        tests_passed++;
    } else {
        std::cout << "[FAIL] " << test_name << std::endl;
        tests_failed++;
    }
}

// Helper to compare two entries
bool entries_equal(const Entry& e1, const Entry& e2, bool check_encrypted = false) {
    if (check_encrypted && e1.is_encrypted != e2.is_encrypted) return false;
    
    // Compare metadata
    if (e1.field_type != e2.field_type) return false;
    if (e1.equality_type != e2.equality_type) return false;
    if (e1.join_attr != e2.join_attr) return false;
    if (e1.original_index != e2.original_index) return false;
    if (e1.local_mult != e2.local_mult) return false;
    if (e1.final_mult != e2.final_mult) return false;
    if (e1.foreign_sum != e2.foreign_sum) return false;
    if (e1.local_cumsum != e2.local_cumsum) return false;
    if (e1.local_interval != e2.local_interval) return false;
    // foreign_cumsum removed - no longer checking
    if (e1.foreign_interval != e2.foreign_interval) return false;
    if (e1.local_weight != e2.local_weight) return false;
    if (e1.dst_idx != e2.dst_idx) return false;
    if (e1.index != e2.index) return false;
    
    // Compare all attributes
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (e1.attributes[i] != e2.attributes[i]) {
            std::cerr << "Attribute[" << i << "] mismatch: " 
                      << e1.attributes[i] << " vs " << e2.attributes[i] << std::endl;
            return false;
        }
    }
    
    // Compare column names
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (strcmp(e1.column_names[i], e2.column_names[i]) != 0) {
            return false;
        }
    }
    
    return true;
}

// Test 1: Basic encrypt/decrypt with small values
void test_basic_small_values(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    // Initialize with small values
    entry.field_type = 1;
    entry.equality_type = 2;
    entry.is_encrypted = false;
    entry.join_attr = 42;
    entry.original_index = 10;
    entry.local_mult = 5;
    entry.final_mult = 15;
    entry.foreign_sum = 20;
    entry.local_cumsum = 25;
    entry.local_interval = 30;
    // foreign_cumsum removed
    entry.foreign_interval = 40;
    entry.local_weight = 45;
    entry.dst_idx = 50;
    entry.index = 55;
    
    // Set attributes with small values
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        entry.attributes[i] = i * 10;
        snprintf(entry.column_names[i], MAX_COLUMN_NAME_LENGTH, "col_%d", i);
    }
    
    original = entry;
    
    // Encrypt
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
    if (status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed with status: " << status << std::endl;
    }
    
    // Verify encrypted
    if (passed && !entry.is_encrypted) {
        passed = false;
        std::cerr << "Entry not marked as encrypted" << std::endl;
    }
    
    // Verify values changed
    if (passed && entry.join_attr == original.join_attr) {
        passed = false;
        std::cerr << "join_attr not encrypted" << std::endl;
    }
    
    // Decrypt
    if (passed) {
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed with status: " << status << std::endl;
        }
    }
    
    // Verify decrypted correctly
    if (passed && !entries_equal(entry, original)) {
        passed = false;
        std::cerr << "Decrypted entry doesn't match original" << std::endl;
    }
    
    report_test_result("Basic encrypt/decrypt with small values", passed);
}

// Test 2: Boundary values within design constraints
void test_boundary_values_within_constraints(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    // Test with values at the boundary of design constraints
    int32_t max_design = INT32_MAX / 2;  // 1073741823
    int32_t min_design = INT32_MIN / 2;  // -1073741824
    
    // Test maximum positive values
    entry.field_type = 3;
    entry.equality_type = 1;
    entry.is_encrypted = false;
    entry.join_attr = max_design;
    entry.original_index = max_design - 1;
    entry.local_mult = max_design - 2;
    entry.final_mult = max_design - 3;
    entry.foreign_sum = max_design - 4;
    entry.local_cumsum = max_design - 5;
    entry.local_interval = max_design - 6;
    // foreign_cumsum removed
    entry.foreign_interval = max_design - 8;
    entry.local_weight = max_design - 9;
    entry.dst_idx = max_design - 10;
    entry.index = max_design - 11;
    
    // Set attributes with boundary values
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        entry.attributes[i] = (i % 2 == 0) ? max_design - i : min_design + i;
        snprintf(entry.column_names[i], MAX_COLUMN_NAME_LENGTH, "bound_%d", i);
    }
    
    original = entry;
    
    // Encrypt
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
    if (status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed for max boundary values: " << status << std::endl;
    }
    
    // Decrypt
    if (passed) {
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed for max boundary values: " << status << std::endl;
        }
    }
    
    // Verify
    if (passed && !entries_equal(entry, original)) {
        passed = false;
        std::cerr << "Boundary values not preserved" << std::endl;
    }
    
    report_test_result("Boundary values within constraints", passed);
}

// Test 3: Values outside design constraints (like our actual data)
void test_values_outside_constraints(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    // Use actual values we saw in the debug output
    entry.field_type = 1;
    entry.equality_type = 2;
    entry.is_encrypted = false;
    entry.join_attr = 1714916990;  // Outside design range
    entry.original_index = 1;
    entry.local_mult = 1;
    entry.final_mult = 1;
    entry.foreign_sum = 0;
    entry.local_cumsum = 0;
    entry.local_interval = 0;
    // foreign_cumsum removed
    entry.foreign_interval = 0;
    entry.local_weight = 0;
    entry.dst_idx = 0;
    entry.index = 0;
    
    // Set attributes with actual problematic values
    entry.attributes[0] = 1255533364;
    entry.attributes[1] = -132464500;
    entry.attributes[2] = -691263418;
    entry.attributes[3] = -189120435;
    entry.attributes[4] = 1506320078;
    entry.attributes[5] = 1714916990;  // The ACCTBAL value
    entry.attributes[6] = -1110691312;
    
    for (int i = 7; i < MAX_ATTRIBUTES; i++) {
        entry.attributes[i] = 0;
    }
    
    strcpy(entry.column_names[0], "S1_S_SUPPKEY");
    strcpy(entry.column_names[1], "S1_S_NAME");
    strcpy(entry.column_names[2], "S1_S_ADDRESS");
    strcpy(entry.column_names[3], "S1_S_NATIONKEY");
    strcpy(entry.column_names[4], "S1_S_PHONE");
    strcpy(entry.column_names[5], "S1_S_ACCTBAL");
    strcpy(entry.column_names[6], "S1_S_COMMENT");
    
    original = entry;
    
    // Encrypt
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
    if (status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed for out-of-bounds values: " << status << std::endl;
    }
    
    // Decrypt
    if (passed) {
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed for out-of-bounds values: " << status << std::endl;
        }
    }
    
    // Verify all values preserved
    if (passed && !entries_equal(entry, original)) {
        passed = false;
        std::cerr << "Out-of-bounds values not preserved correctly" << std::endl;
        
        // Debug output
        std::cerr << "Original join_attr: " << original.join_attr 
                  << ", Decrypted: " << entry.join_attr << std::endl;
        std::cerr << "Original attr[5]: " << original.attributes[5] 
                  << ", Decrypted: " << entry.attributes[5] << std::endl;
    }
    
    report_test_result("Values outside design constraints", passed);
}

// Test 4: All zero values
void test_zero_values(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    memset(&entry, 0, sizeof(Entry));
    
    original = entry;
    
    // Encrypt
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
    if (status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed for zero values: " << status << std::endl;
    }
    
    // Decrypt
    if (passed) {
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed for zero values: " << status << std::endl;
        }
    }
    
    // Verify
    if (passed && !entries_equal(entry, original)) {
        passed = false;
        std::cerr << "Zero values not preserved" << std::endl;
    }
    
    report_test_result("All zero values", passed);
}

// Test 5: Negative values
void test_negative_values(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    entry.field_type = -1;
    entry.equality_type = -2;
    entry.is_encrypted = false;
    entry.join_attr = -1000000;
    entry.original_index = -999999;
    entry.local_mult = -5;
    entry.final_mult = -15;
    entry.foreign_sum = -20;
    entry.local_cumsum = -25;
    entry.local_interval = -30;
    // foreign_cumsum removed
    entry.foreign_interval = -40;
    entry.local_weight = -45;
    entry.dst_idx = -50;
    entry.index = -55;
    
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        entry.attributes[i] = -i * 10000;
        snprintf(entry.column_names[i], MAX_COLUMN_NAME_LENGTH, "neg_%d", i);
    }
    
    original = entry;
    
    // Encrypt
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
    if (status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed for negative values: " << status << std::endl;
    }
    
    // Decrypt
    if (passed) {
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed for negative values: " << status << std::endl;
        }
    }
    
    // Verify
    if (passed && !entries_equal(entry, original)) {
        passed = false;
        std::cerr << "Negative values not preserved" << std::endl;
    }
    
    report_test_result("Negative values", passed);
}

// Test 6: Multiple encrypt/decrypt cycles
void test_multiple_cycles(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    // Initialize with random values
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int32_t> dis(-1000000, 1000000);
    
    entry.field_type = dis(gen);
    entry.equality_type = dis(gen);
    entry.is_encrypted = false;
    entry.join_attr = dis(gen);
    entry.original_index = dis(gen);
    entry.local_mult = dis(gen);
    entry.final_mult = dis(gen);
    
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        entry.attributes[i] = dis(gen);
        snprintf(entry.column_names[i], MAX_COLUMN_NAME_LENGTH, "cycle_%d", i);
    }
    
    original = entry;
    
    // Perform multiple encrypt/decrypt cycles
    const int num_cycles = 10;
    for (int cycle = 0; cycle < num_cycles; cycle++) {
        // Encrypt
        crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Encryption failed at cycle " << cycle << ": " << status << std::endl;
            break;
        }
        
        // Decrypt
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed at cycle " << cycle << ": " << status << std::endl;
            break;
        }
        
        // Verify after each cycle
        if (!entries_equal(entry, original)) {
            passed = false;
            std::cerr << "Values corrupted after cycle " << cycle << std::endl;
            break;
        }
    }
    
    report_test_result("Multiple encrypt/decrypt cycles", passed);
}

// Test 7: Specific attribute preservation test
void test_attribute_preservation(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    // Set very specific values for each attribute to detect any corruption
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        entry.attributes[i] = (i + 1) * 111111;  // 111111, 222222, 333333, etc.
        snprintf(entry.column_names[i], MAX_COLUMN_NAME_LENGTH, "attr_%d", i);
    }
    
    // Set other fields
    entry.field_type = 5;
    entry.equality_type = 3;
    entry.is_encrypted = false;
    entry.join_attr = 999999;
    entry.original_index = 888888;
    entry.local_mult = 777777;
    
    original = entry;
    
    // Encrypt
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
    if (status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed: " << status << std::endl;
    }
    
    // Check that attributes are actually encrypted (changed)
    if (passed) {
        bool any_unchanged = false;
        for (int i = 0; i < MAX_ATTRIBUTES; i++) {
            if (entry.attributes[i] == original.attributes[i]) {
                any_unchanged = true;
                std::cerr << "Warning: attribute[" << i << "] unchanged after encryption: " 
                          << entry.attributes[i] << std::endl;
            }
        }
        if (any_unchanged) {
            std::cerr << "Some attributes were not encrypted" << std::endl;
        }
    }
    
    // Decrypt
    if (passed) {
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed: " << status << std::endl;
        }
    }
    
    // Verify each attribute individually
    if (passed) {
        for (int i = 0; i < MAX_ATTRIBUTES; i++) {
            if (entry.attributes[i] != original.attributes[i]) {
                passed = false;
                std::cerr << "Attribute[" << i << "] corrupted: expected " 
                          << original.attributes[i] << " got " << entry.attributes[i] << std::endl;
            }
        }
    }
    
    report_test_result("Attribute preservation", passed);
}

// Test 8: Test with actual TPCH data values
void test_tpch_actual_values(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    // Use actual plaintext values from supplier1.csv
    entry.field_type = 1;
    entry.equality_type = 0;
    entry.is_encrypted = false;
    
    // Test each row's actual values
    int test_cases[][7] = {
        {1, 792906294, 317827973, 17, 971385163, 575594, 622797579},
        {2, 549623314, 849027485, 5, 58126162, 403268, 958596215},
        {3, 458785, 31140224, 1, 73682198, 419239, 154263564},
        {4, 829353804, 938198330, 15, 991041614, 464108, 693080424},
        {5, 64952534, 366176741, 11, 103869805, -28383, 275262022}
    };
    
    for (int row = 0; row < 5; row++) {
        // Set attributes from actual data
        for (int col = 0; col < 7; col++) {
            entry.attributes[col] = test_cases[row][col];
        }
        
        entry.join_attr = entry.attributes[5];  // ACCTBAL is column 5
        entry.original_index = row;
        entry.local_mult = 1;
        entry.final_mult = 1;
        
        original = entry;
        
        // Encrypt
        crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Encryption failed for row " << row << ": " << status << std::endl;
            break;
        }
        
        // Decrypt
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed for row " << row << ": " << status << std::endl;
            break;
        }
        
        // Verify ACCTBAL specifically
        if (entry.attributes[5] != original.attributes[5]) {
            passed = false;
            std::cerr << "Row " << row << " ACCTBAL corrupted: expected " 
                      << original.attributes[5] << " got " << entry.attributes[5] << std::endl;
            break;
        }
        
        // Verify all values
        if (!entries_equal(entry, original)) {
            passed = false;
            std::cerr << "Row " << row << " data corrupted" << std::endl;
            break;
        }
    }
    
    report_test_result("TPCH actual values", passed);
}

// Test 9: Test full INT32 range
void test_full_int32_range(sgx_enclave_id_t eid) {
    bool passed = true;
    Entry entry, original;
    
    // Test with INT32_MAX and INT32_MIN
    entry.field_type = 1;
    entry.equality_type = 2;
    entry.is_encrypted = false;
    entry.join_attr = INT32_MAX;
    entry.original_index = INT32_MIN;
    entry.local_mult = INT32_MAX;
    entry.final_mult = INT32_MIN;
    
    entry.attributes[0] = INT32_MAX;
    entry.attributes[1] = INT32_MIN;
    entry.attributes[2] = INT32_MAX - 1;
    entry.attributes[3] = INT32_MIN + 1;
    entry.attributes[4] = 0;
    entry.attributes[5] = -1;
    entry.attributes[6] = 1;
    
    original = entry;
    
    // Encrypt
    crypto_status_t status = CryptoUtils::encrypt_entry(entry, eid);
    if (status != CRYPTO_SUCCESS) {
        passed = false;
        std::cerr << "Encryption failed for INT32 extremes: " << status << std::endl;
    }
    
    // Decrypt
    if (passed) {
        status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            passed = false;
            std::cerr << "Decryption failed for INT32 extremes: " << status << std::endl;
        }
    }
    
    // Verify
    if (passed && !entries_equal(entry, original)) {
        passed = false;
        std::cerr << "INT32 extreme values not preserved" << std::endl;
    }
    
    report_test_result("Full INT32 range", passed);
}

int main(int argc, char* argv[]) {
    // Initialize enclave
    sgx_enclave_id_t eid = 0;
    sgx_launch_token_t token = {0};
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    int updated = 0;
    
    // Get enclave path from argv or use default
    const char* enclave_path = (argc > 1) ? argv[1] : "../../enclave.signed.so";
    
    ret = sgx_create_enclave(enclave_path, SGX_DEBUG_FLAG, &token, &updated, &eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: 0x" << std::hex << ret << std::endl;
        return -1;
    }
    
    std::cout << "\n=== Comprehensive Encryption/Decryption Tests ===" << std::endl;
    std::cout << "Testing all value ranges and edge cases...\n" << std::endl;
    
    // Run all tests
    test_basic_small_values(eid);
    test_boundary_values_within_constraints(eid);
    test_values_outside_constraints(eid);
    test_zero_values(eid);
    test_negative_values(eid);
    test_multiple_cycles(eid);
    test_attribute_preservation(eid);
    test_tpch_actual_values(eid);
    test_full_int32_range(eid);
    
    // Summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Tests passed: " << tests_passed << std::endl;
    std::cout << "Tests failed: " << tests_failed << std::endl;
    
    if (tests_failed == 0) {
        std::cout << "\nAll tests PASSED! Encryption/decryption is working correctly." << std::endl;
    } else {
        std::cout << "\nSome tests FAILED. Check the output above for details." << std::endl;
    }
    
    // Destroy enclave
    sgx_destroy_enclave(eid);
    
    return (tests_failed == 0) ? 0 : 1;
}