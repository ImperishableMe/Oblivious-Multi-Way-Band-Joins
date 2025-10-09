#include <iostream>
#include <cstring>
#include "sgx_compat/sgx_urts.h"
#include "app/Enclave_u.h"
#include "enclave/enclave_types.h"

// External test reporting function
extern void report_test_result(const std::string& test_name, bool passed);

// Helper to create entries for testing
void create_pair(entry_t& e1, entry_t& e2) {
    memset(&e1, 0, sizeof(entry_t));
    memset(&e2, 0, sizeof(entry_t));
}

// Test comparator_join_attr
void test_comparator_join_attr(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    
    // Test 1: e1.join_attr < e2.join_attr - should not swap
    create_pair(e1, e2);
    e1.join_attr = 10;
    e2.join_attr = 20;
    e1.original_index = 1;
    e2.original_index = 2;
    
    sgx_status_t ret = ecall_comparator_join_attr(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e1.join_attr != 10) {
        passed = false;
        std::cerr << "Should not swap when e1 < e2" << std::endl;
    }
    
    // Test 2: e1.join_attr > e2.join_attr - should swap
    create_pair(e1, e2);
    e1.join_attr = 30;
    e2.join_attr = 20;
    e1.original_index = 1;
    e2.original_index = 2;
    
    ret = ecall_comparator_join_attr(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e1.join_attr != 20) {
        passed = false;
        std::cerr << "Should swap when e1 > e2, e1.join_attr is " << e1.join_attr << std::endl;
    }
    
    // Test 3: Equal join_attr, check precedence
    create_pair(e1, e2);
    e1.join_attr = 20;
    e2.join_attr = 20;
    e1.field_type = SOURCE;     // precedence 2
    e2.field_type = START;      
    e2.equality_type = EQ;      // precedence 1
    
    ret = ecall_comparator_join_attr(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e1.field_type != START) {
        passed = false;
        std::cerr << "Should swap based on precedence" << std::endl;
    }
    
    report_test_result("Comparator Join Attribute", passed);
}

// Test comparator_pairwise
void test_comparator_pairwise(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    
    // Test 1: TARGET before SOURCE
    create_pair(e1, e2);
    e1.field_type = SOURCE;
    e2.field_type = START;  // TARGET type
    e1.original_index = 1;
    e2.original_index = 2;
    
    sgx_status_t ret = ecall_comparator_pairwise(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e1.field_type != START) {
        passed = false;
        std::cerr << "TARGET should come before SOURCE" << std::endl;
    }
    
    // Test 2: Same type, sort by original_index
    create_pair(e1, e2);
    e1.field_type = SOURCE;
    e2.field_type = SOURCE;
    e1.original_index = 20;
    e2.original_index = 10;
    
    ret = ecall_comparator_pairwise(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e1.original_index != 10) {
        passed = false;
        std::cerr << "Should sort by original_index" << std::endl;
    }
    
    // Test 3: Same index, START before END
    create_pair(e1, e2);
    e1.field_type = END;
    e2.field_type = START;
    e1.original_index = 10;
    e2.original_index = 10;
    
    ret = ecall_comparator_pairwise(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e1.field_type != START) {
        passed = false;
        std::cerr << "START should come before END for same index" << std::endl;
    }
    
    report_test_result("Comparator Pairwise", passed);
}

// Test comparator_end_first
void test_comparator_end_first(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    
    // Test 1: END before non-END
    create_pair(e1, e2);
    e1.field_type = SOURCE;
    e2.field_type = END;
    e1.original_index = 1;
    e2.original_index = 2;
    
    sgx_status_t ret = ecall_comparator_end_first(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e1.field_type != END) {
        passed = false;
        std::cerr << "END should come first" << std::endl;
    }
    
    // Test 2: Both END, sort by index
    create_pair(e1, e2);
    e1.field_type = END;
    e2.field_type = END;
    e1.original_index = 20;
    e2.original_index = 10;
    
    ret = ecall_comparator_end_first(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e1.original_index != 10) {
        passed = false;
        std::cerr << "Should sort by original_index when both END" << std::endl;
    }
    
    report_test_result("Comparator END First", passed);
}

// Test comparator_join_then_other
void test_comparator_join_then_other(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    
    // Test 1: Different join_attr
    create_pair(e1, e2);
    e1.join_attr = 30;
    e2.join_attr = 20;
    e1.original_index = 100;
    e2.original_index = 1;
    
    sgx_status_t ret = ecall_comparator_join_then_other(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e1.join_attr != 20) {
        passed = false;
        std::cerr << "Should sort by join_attr first" << std::endl;
    }
    
    // Test 2: Same join_attr, different index
    create_pair(e1, e2);
    e1.join_attr = 20;
    e2.join_attr = 20;
    e1.original_index = 100;
    e2.original_index = 50;
    
    ret = ecall_comparator_join_then_other(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e1.original_index != 50) {
        passed = false;
        std::cerr << "Should sort by original_index when join_attr equal" << std::endl;
    }
    
    report_test_result("Comparator Join Then Other", passed);
}

// Test comparator_original_index
void test_comparator_original_index(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    
    create_pair(e1, e2);
    e1.original_index = 100;
    e2.original_index = 50;
    e1.join_attr = 999;  // Should be ignored
    e2.join_attr = 1;
    
    sgx_status_t ret = ecall_comparator_original_index(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e1.original_index != 50) {
        passed = false;
        std::cerr << "Should sort by original_index only" << std::endl;
    }
    
    report_test_result("Comparator Original Index", passed);
}

// Test comparator_alignment_key
void test_comparator_alignment_key(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    
    create_pair(e1, e2);
    e1.alignment_key = 200;
    e2.alignment_key = 100;
    e1.original_index = 1;  // Should be ignored
    e2.original_index = 999;
    
    sgx_status_t ret = ecall_comparator_alignment_key(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e1.alignment_key != 100) {
        passed = false;
        std::cerr << "Should sort by alignment_key only" << std::endl;
    }
    
    report_test_result("Comparator Alignment Key", passed);
}

// Test infinity value handling
void test_infinity_handling(sgx_enclave_id_t eid) {
    bool passed = true;
    entry_t e1, e2;
    
    // Test negative infinity
    create_pair(e1, e2);
    e1.join_attr = JOIN_ATTR_NEG_INF;
    e2.join_attr = 0;
    
    sgx_status_t ret = ecall_comparator_join_attr(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
        std::cerr << "SGX call failed" << std::endl;
    }
    
    if (passed && e1.join_attr != JOIN_ATTR_NEG_INF) {
        passed = false;
        std::cerr << "-∞ should stay first" << std::endl;
    }
    
    // Test positive infinity
    create_pair(e1, e2);
    e1.join_attr = 0;
    e2.join_attr = JOIN_ATTR_POS_INF;
    
    ret = ecall_comparator_join_attr(eid, &e1, &e2);
    if (ret != SGX_SUCCESS) {
        passed = false;
    }
    
    if (passed && e2.join_attr != JOIN_ATTR_POS_INF) {
        passed = false;
        std::cerr << "+∞ should stay last" << std::endl;
    }
    
    report_test_result("Infinity Value Handling", passed);
}

// Main comparator test suite
void run_comparator_tests(sgx_enclave_id_t eid) {
    test_comparator_join_attr(eid);
    test_comparator_pairwise(eid);
    test_comparator_end_first(eid);
    test_comparator_join_then_other(eid);
    test_comparator_original_index(eid);
    test_comparator_alignment_key(eid);
    test_infinity_handling(eid);
}