#include <iostream>
#include <string>
#include <vector>
#include "sgx_urts.h"
#include "app/Enclave_u.h"

// Test suite declarations
void run_encryption_tests(sgx_enclave_id_t eid);
void run_window_tests(sgx_enclave_id_t eid);
void run_comparator_tests(sgx_enclave_id_t eid);

// Global test counters
int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

// Test result reporting
void report_test_result(const std::string& test_name, bool passed) {
    g_tests_run++;
    if (passed) {
        g_tests_passed++;
        std::cout << "[PASS] " << test_name << std::endl;
    } else {
        g_tests_failed++;
        std::cout << "[FAIL] " << test_name << std::endl;
    }
}

// Initialize enclave
sgx_enclave_id_t initialize_enclave() {
    sgx_enclave_id_t eid;
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    // Use the enclave from the parent directory
    std::string enclave_file = "../enclave.signed.so";
    
    // Create enclave
    ret = sgx_create_enclave(
        enclave_file.c_str(),
        SGX_DEBUG_FLAG,
        nullptr,
        nullptr,
        &eid,
        nullptr
    );
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Error: Failed to create enclave (error code: 0x" 
                  << std::hex << ret << ")" << std::endl;
        std::cerr << "Make sure the enclave is built: make -C .." << std::endl;
        exit(1);
    }
    
    std::cout << "Enclave created successfully. ID: " << eid << std::endl;
    return eid;
}

// Destroy enclave
void destroy_enclave(sgx_enclave_id_t eid) {
    sgx_status_t ret = sgx_destroy_enclave(eid);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Warning: Failed to destroy enclave properly" << std::endl;
    } else {
        std::cout << "Enclave destroyed successfully." << std::endl;
    }
}

// Print test summary
void print_summary() {
    std::cout << "\n====================================" << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "Total tests run: " << g_tests_run << std::endl;
    std::cout << "Tests passed: " << g_tests_passed << std::endl;
    std::cout << "Tests failed: " << g_tests_failed << std::endl;
    
    if (g_tests_failed == 0) {
        std::cout << "\nAll tests PASSED! ✓" << std::endl;
    } else {
        std::cout << "\nSome tests FAILED. ✗" << std::endl;
    }
}

// Main test runner
int main(int argc, char* argv[]) {
    std::cout << "SGX Unit Test Runner" << std::endl;
    std::cout << "====================================" << std::endl;
    
    // Parse command line arguments
    std::string suite = "all";
    if (argc > 1) {
        if (std::string(argv[1]) == "--suite" && argc > 2) {
            suite = argv[2];
        }
    }
    
    // Initialize enclave
    std::cout << "\nInitializing SGX enclave..." << std::endl;
    sgx_enclave_id_t eid = initialize_enclave();
    
    // Run test suites
    try {
        if (suite == "all" || suite == "encryption") {
            std::cout << "\n--- Running Encryption Tests ---" << std::endl;
            run_encryption_tests(eid);
        }
        
        if (suite == "all" || suite == "window") {
            std::cout << "\n--- Running Window Function Tests ---" << std::endl;
            run_window_tests(eid);
        }
        
        if (suite == "all" || suite == "comparators") {
            std::cout << "\n--- Running Comparator Tests ---" << std::endl;
            run_comparator_tests(eid);
        }
        
        if (suite != "all" && suite != "encryption" && 
            suite != "window" && suite != "comparators") {
            std::cout << "Unknown test suite: " << suite << std::endl;
            std::cout << "Available suites: all, encryption, window, comparators" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        g_tests_failed++;
    }
    
    // Print summary
    print_summary();
    
    // Clean up
    std::cout << "\nCleaning up..." << std::endl;
    destroy_enclave(eid);
    
    // Return non-zero if any tests failed
    return (g_tests_failed > 0) ? 1 : 0;
}