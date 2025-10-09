#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include "../../src/algorithms/distribute_expand.h"
#include "../../src/core/table.h"
#include "../../src/core/join_tree_node.h"
#include "../../app/file_io/table_io.h"
#include "constants.h"
#include "debug_util.h"
#include "sgx_compat/sgx_urts.h"

#define ENCLAVE_FILENAME "enclave.signed.so"

class DistributeExpandTest {
public:
    bool RunStandaloneTest(sgx_enclave_id_t eid) {
        std::cout << "\n=== Testing Distribute-Expand Phase ===" << std::endl;
        
        // Initialize debug session
        debug_init_session("distribute_expand_test");
        
        // Create a test table with known multiplicities
        Table test_table;
        test_table.set_table_name("test_table");
        
        // Add entries with various final_mult values
        Entry e1;
        e1.field_type = TARGET;
        e1.final_mult = 3;
        e1.original_index = 0;
        e1.attributes.push_back(100);  // Data value
        e1.column_names.push_back("value");
        test_table.add_entry(e1);
        
        Entry e2;
        e2.field_type = TARGET;
        e2.final_mult = 0;  // Should disappear
        e2.original_index = 1;
        e2.attributes.push_back(200);
        e2.column_names.push_back("value");
        test_table.add_entry(e2);
        
        Entry e3;
        e3.field_type = TARGET;
        e3.final_mult = 2;
        e3.original_index = 2;
        e3.attributes.push_back(300);
        e3.column_names.push_back("value");
        test_table.add_entry(e3);
        
        Entry e4;
        e4.field_type = TARGET;
        e4.final_mult = 1;
        e4.original_index = 3;
        e4.attributes.push_back(400);
        e4.column_names.push_back("value");
        test_table.add_entry(e4);
        
        std::cout << "Original table:" << std::endl;
        for (size_t i = 0; i < test_table.size(); i++) {
            std::cout << "  Entry " << i << ": value=" << test_table[i].attributes[0]
                     << ", final_mult=" << test_table[i].final_mult << std::endl;
        }
        
        // Create a simple join tree with single node
        auto root = std::make_shared<JoinTreeNode>("test_table", test_table);
        
        // Run distribute-expand
        std::cout << "\nRunning distribute-expand..." << std::endl;
        DistributeExpand::Execute(root, eid);
        
        // Get expanded table
        Table& expanded = root->get_table();
        
        // Verify results
        std::cout << "\nExpanded table:" << std::endl;
        size_t expected_size = 3 + 0 + 2 + 1;  // Sum of final_mult values
        
        if (expanded.size() != expected_size) {
            std::cerr << "ERROR: Expected size " << expected_size 
                     << " but got " << expanded.size() << std::endl;
            debug_close_session();
            return false;
        }
        
        // Count occurrences of each value
        int count_100 = 0, count_200 = 0, count_300 = 0, count_400 = 0;
        for (size_t i = 0; i < expanded.size(); i++) {
            if (expanded[i].attributes.empty()) {
                std::cout << "  Entry " << i << ": NO ATTRIBUTES (padding?)" << std::endl;
                continue;
            }
            int value = expanded[i].attributes[0];
            std::cout << "  Entry " << i << ": value=" << value << std::endl;
            
            if (value == 100) count_100++;
            else if (value == 200) count_200++;
            else if (value == 300) count_300++;
            else if (value == 400) count_400++;
        }
        
        // Verify counts match final_mult values
        bool success = true;
        if (count_100 != 3) {
            std::cerr << "ERROR: Expected 3 copies of value 100, got " << count_100 << std::endl;
            success = false;
        }
        if (count_200 != 0) {
            std::cerr << "ERROR: Expected 0 copies of value 200, got " << count_200 << std::endl;
            success = false;
        }
        if (count_300 != 2) {
            std::cerr << "ERROR: Expected 2 copies of value 300, got " << count_300 << std::endl;
            success = false;
        }
        if (count_400 != 1) {
            std::cerr << "ERROR: Expected 1 copy of value 400, got " << count_400 << std::endl;
            success = false;
        }
        
        if (success) {
            std::cout << "\n✓ Distribute-expand test passed!" << std::endl;
        } else {
            std::cout << "\n✗ Distribute-expand test failed!" << std::endl;
        }
        
        debug_close_session();
        return success;
    }
    
    bool RunIntegrationTest(const std::string& query_file, 
                           const std::string& data_dir,
                           sgx_enclave_id_t eid) {
        // Initialize debug session with query file name
        size_t last_slash = query_file.find_last_of("/");
        std::string test_name = (last_slash != std::string::npos) 
                                ? query_file.substr(last_slash + 1) 
                                : query_file;
        debug_init_session(("distribute_expand_" + test_name).c_str());
        
        // This would run the full pipeline including distribute-expand
        // For now, just return true as placeholder
        std::cout << "\nIntegration test not yet implemented" << std::endl;
        
        debug_close_session();
        return true;
    }
};

int main(int argc, char* argv[]) {
    // Initialize SGX enclave
    sgx_enclave_id_t eid = 0;
    sgx_status_t ret = SGX_SUCCESS;
    
    ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG,
                             nullptr, nullptr, &eid, nullptr);
    
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: " << ret << std::endl;
        return 1;
    }
    
    std::cout << "SGX Enclave initialized (ID: " << eid << ")" << std::endl;
    
    DistributeExpandTest test;
    bool success = false;
    
    if (argc == 1) {
        // Run standalone test
        success = test.RunStandaloneTest(eid);
    } else if (argc == 3) {
        // Run integration test with query file and data directory
        success = test.RunIntegrationTest(argv[1], argv[2], eid);
    } else {
        std::cerr << "Usage: " << argv[0] << " [query_file data_dir]" << std::endl;
        sgx_destroy_enclave(eid);
        return 1;
    }
    
    // Destroy enclave
    sgx_destroy_enclave(eid);
    std::cout << "SGX Enclave destroyed" << std::endl;
    
    return success ? 0 : 1;
}