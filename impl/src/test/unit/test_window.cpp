#include <iostream>
#include <cstring>
#include "sgx_urts.h"
#include "app/Enclave_u.h"
#include "enclave/enclave_types.h"

// External test reporting function
extern void report_test_result(const std::string& test_name, bool passed);

// Test window_set_original_index
void test_window_set_original_index(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    memset(&e1, 0, sizeof(entry_t));
    memset(&e2, 0, sizeof(entry_t));
    
    e1.original_index = 10;
    e2.original_index = 0;  // Should be set to e1.original_index + 1
    
    sgx_status_t ret = ecall_window_set_original_index(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e2.original_index != 11) {
        passed = false;
        std::cerr << "e2.original_index should be 11, got " << e2.original_index << std::endl;
    }
    
    report_test_result("Window Set Original Index", passed);
}

// Test window_compute_local_sum
void test_window_compute_local_sum(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    memset(&e1, 0, sizeof(entry_t));
    memset(&e2, 0, sizeof(entry_t));
    
    // Test with SOURCE entry
    e1.local_cumsum = 100;
    e2.field_type = SOURCE;
    e2.local_mult = 50;
    
    sgx_status_t ret = ecall_window_compute_local_sum(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e2.local_cumsum != 150) {
        passed = false;
        std::cerr << "SOURCE: Expected cumsum 150, got " << e2.local_cumsum << std::endl;
    }
    
    // Test with non-SOURCE entry
    memset(&e2, 0, sizeof(entry_t));
    e2.field_type = START;
    e2.local_mult = 50;
    
    ret = ecall_window_compute_local_sum(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e2.local_cumsum != 100) {
        passed = false;
        std::cerr << "START: Expected cumsum 100, got " << e2.local_cumsum << std::endl;
    }
    
    report_test_result("Window Compute Local Sum", passed);
}

// Test window_compute_local_interval
void test_window_compute_local_interval(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    memset(&e1, 0, sizeof(entry_t));
    memset(&e2, 0, sizeof(entry_t));
    
    // Test START/END pair
    e1.field_type = START;
    e1.local_cumsum = 100;
    e2.field_type = END;
    e2.local_cumsum = 250;
    e2.local_interval = 0;
    
    sgx_status_t ret = ecall_window_compute_local_interval(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e2.local_interval != 150) {
        passed = false;
        std::cerr << "START/END: Expected interval 150, got " << e2.local_interval << std::endl;
    }
    
    // Test non-matching pair
    e1.field_type = SOURCE;
    e2.local_interval = 999;  // Should remain unchanged
    
    ret = ecall_window_compute_local_interval(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e2.local_interval != 999) {
        passed = false;
        std::cerr << "Non-pair: Interval should remain 999, got " << e2.local_interval << std::endl;
    }
    
    report_test_result("Window Compute Local Interval", passed);
}

// Test window_compute_foreign_sum
void test_window_compute_foreign_sum(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    memset(&e1, 0, sizeof(entry_t));
    memset(&e2, 0, sizeof(entry_t));
    
    // Test START entry - should add to weight
    e1.local_weight = 100;
    e1.foreign_sum = 50;
    e2.field_type = START;
    e2.local_mult = 25;
    
    sgx_status_t ret = ecall_window_compute_foreign_sum(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e2.local_weight != 125) {
        passed = false;
        std::cerr << "START: Expected weight 125, got " << e2.local_weight << std::endl;
    }
    if (passed && e2.foreign_sum != 50) {
        passed = false;
        std::cerr << "START: Expected foreign_sum 50, got " << e2.foreign_sum << std::endl;
    }
    
    // Test END entry - should subtract from weight
    e1.local_weight = 100;
    e2.field_type = END;
    e2.local_mult = 25;
    
    ret = ecall_window_compute_foreign_sum(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e2.local_weight != 75) {
        passed = false;
        std::cerr << "END: Expected weight 75, got " << e2.local_weight << std::endl;
    }
    
    // Test SOURCE entry - weight unchanged, foreign_sum updated
    e1.local_weight = 100;
    e1.foreign_sum = 50;
    e2.field_type = SOURCE;
    e2.final_mult = 200;
    
    ret = ecall_window_compute_foreign_sum(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e2.local_weight != 100) {
        passed = false;
        std::cerr << "SOURCE: Weight should remain 100, got " << e2.local_weight << std::endl;
    }
    // foreign_sum should be 50 + 200/100 = 52
    if (passed && e2.foreign_sum != 52) {
        passed = false;
        std::cerr << "SOURCE: Expected foreign_sum 52, got " << e2.foreign_sum << std::endl;
    }
    
    report_test_result("Window Compute Foreign Sum", passed);
}

// Test window_compute_foreign_interval
void test_window_compute_foreign_interval(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    memset(&e1, 0, sizeof(entry_t));
    memset(&e2, 0, sizeof(entry_t));
    
    // Test START/END pair
    e1.field_type = START;
    e1.foreign_sum = 100;
    e2.field_type = END;
    e2.foreign_sum = 300;
    e2.foreign_interval = 0;
    e2.foreign_sum = 0;
    
    sgx_status_t ret = ecall_window_compute_foreign_interval(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e2.foreign_interval != 200) {
        passed = false;
        std::cerr << "START/END: Expected foreign_interval 200, got " << e2.foreign_interval << std::endl;
    }
    if (passed && e2.foreign_sum != 100) {
        passed = false;
        std::cerr << "START/END: Expected foreign_sum 100, got " << e2.foreign_sum << std::endl;
    }
    
    // Test non-matching pair
    e1.field_type = SOURCE;
    e2.foreign_interval = 999;
    e2.foreign_sum = 888;
    
    ret = ecall_window_compute_foreign_interval(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e2.foreign_interval != 999) {
        passed = false;
        std::cerr << "Non-pair: foreign_interval should remain 999, got " << e2.foreign_interval << std::endl;
    }
    if (passed && e2.foreign_sum != 888) {
        passed = false;
        std::cerr << "Non-pair: foreign_sum should remain 888, got " << e2.foreign_sum << std::endl;
    }
    
    report_test_result("Window Compute Foreign Interval", passed);
}

// Test update_target_multiplicity
void test_update_target_multiplicity(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t target, source;
    memset(&target, 0, sizeof(entry_t));
    memset(&source, 0, sizeof(entry_t));
    
    target.local_mult = 5;
    source.local_interval = 8;
    
    sgx_status_t ret = ecall_update_target_multiplicity(eid, &source, &target);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && target.local_mult != 40) {
        passed = false;
        std::cerr << "Expected local_mult 40, got " << target.local_mult << std::endl;
    }
    
    report_test_result("Update Target Multiplicity", passed);
}

// Test update_target_final_multiplicity
void test_update_target_final_multiplicity(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t target, source;
    memset(&target, 0, sizeof(entry_t));
    memset(&source, 0, sizeof(entry_t));
    
    target.local_mult = 5;
    source.foreign_interval = 7;
    source.foreign_sum = 123;
    
    sgx_status_t ret = ecall_update_target_final_multiplicity(eid, &source, &target);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && target.final_mult != 35) {
        passed = false;
        std::cerr << "Expected final_mult 35, got " << target.final_mult << std::endl;
    }
    if (passed && target.foreign_sum != 123) {
        passed = false;
        std::cerr << "Expected foreign_sum 123, got " << target.foreign_sum << std::endl;
    }
    
    report_test_result("Update Target Final Multiplicity", passed);
}

// Main window function test suite
void run_window_tests(sgx_enclave_id_t eid) {
    test_window_set_original_index(eid);
    test_window_compute_local_sum(eid);
    test_window_compute_local_interval(eid);
    test_window_compute_foreign_sum(eid);
    test_window_compute_foreign_interval(eid);
    test_update_target_multiplicity(eid);
    test_update_target_final_multiplicity(eid);
}