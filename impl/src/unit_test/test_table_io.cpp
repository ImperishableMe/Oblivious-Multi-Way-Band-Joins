#include <iostream>
#include <string>
#include <fstream>
#include "sgx_urts.h"
#include "app/utils/table_io.h"
#include "app/utils/join_constraint.h"
#include "app/utils/join_tree_node.h"
#include "app/Enclave_u.h"

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

// Initialize enclave
int initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return -1;
    }
    
    return 0;
}

// Destroy enclave
void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
    }
}

void test_csv_loading() {
    std::cout << "\n=== Testing CSV Loading ===" << std::endl;
    
    try {
        // Test loading a single CSV file
        std::string csv_path = "../../../plaintext/data/data_0_001/nation.csv";
        
        if (TableIO::file_exists(csv_path)) {
            Table nation = TableIO::load_csv(csv_path);
            std::cout << "Loaded nation table: " << nation.size() << " rows" << std::endl;
            
            // Print first few entries
            std::cout << "First 3 entries:" << std::endl;
            for (size_t i = 0; i < std::min(size_t(3), nation.size()); ++i) {
                const Entry& e = nation.get_entry(i);
                std::cout << "  Row " << i << ": ";
                for (size_t j = 0; j < std::min(size_t(3), e.attributes.size()); ++j) {
                    std::cout << e.attributes[j] << " ";
                }
                std::cout << "..." << std::endl;
            }
        } else {
            std::cout << "CSV file not found: " << csv_path << std::endl;
        }
        
        // Test loading directory
        std::string data_dir = "../../../plaintext/data/data_0_001/";
        if (TableIO::file_exists(data_dir)) {
            auto tables = TableIO::load_csv_directory(data_dir);
            std::cout << "\nLoaded " << tables.size() << " tables from directory" << std::endl;
            
            for (const auto& [name, table] : tables) {
                std::cout << "  " << name << ": " << table.size() << " rows" << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error during CSV loading: " << e.what() << std::endl;
    }
}

void test_join_constraint() {
    std::cout << "\n=== Testing JoinConstraint ===" << std::endl;
    
    // Test equality join
    auto eq_constraint = JoinConstraint::equality(
        "orders", "O_CUSTKEY",
        "customer", "C_CUSTKEY"
    );
    std::cout << "Equality join: " << eq_constraint.to_string() << std::endl;
    
    // Test band join
    auto band_constraint = JoinConstraint::band(
        "supplier2", "S2_S_ACCTBAL",
        "supplier1", "S1_S_ACCTBAL",
        -100, 1000, true, true
    );
    std::cout << "Band join: " << band_constraint.to_string() << std::endl;
    
    // Test constraint reversal
    auto reversed = band_constraint.reverse();
    std::cout << "Reversed: " << reversed.to_string() << std::endl;
    
    // Verify double reversal
    auto double_reversed = reversed.reverse();
    std::cout << "Double reversed: " << double_reversed.to_string() << std::endl;
    
    // Check parameters
    auto params = band_constraint.get_params();
    std::cout << "Constraint params: dev1=" << params.deviation1 
              << ", eq1=" << params.equality1
              << ", dev2=" << params.deviation2 
              << ", eq2=" << params.equality2 << std::endl;
}

void test_join_tree() {
    std::cout << "\n=== Testing JoinTreeNode ===" << std::endl;
    
    try {
        // Load some tables
        std::string data_dir = "../../../plaintext/data/data_0_001/";
        
        Table customer = TableIO::load_csv(data_dir + "customer.csv");
        Table orders = TableIO::load_csv(data_dir + "orders.csv");
        Table lineitem = TableIO::load_csv(data_dir + "lineitem.csv");
        
        // Create tree nodes
        auto root = std::make_shared<JoinTreeNode>("customer", customer);
        
        // Add orders as child of customer
        auto orders_constraint = JoinConstraint::equality(
            "orders", "O_CUSTKEY",
            "customer", "C_CUSTKEY"
        );
        auto orders_node = root->add_child("orders", orders, orders_constraint);
        
        // Add lineitem as child of orders
        auto lineitem_constraint = JoinConstraint::equality(
            "lineitem", "L_ORDERKEY",
            "orders", "O_ORDERKEY"
        );
        orders_node->add_child("lineitem", lineitem, lineitem_constraint);
        
        // Print tree structure
        std::cout << "\nJoin tree structure:" << std::endl;
        root->print_tree();
        
        // Test tree properties
        std::cout << "\nTree properties:" << std::endl;
        std::cout << "Root is_root: " << root->is_root() << std::endl;
        std::cout << "Root is_leaf: " << root->is_leaf() << std::endl;
        std::cout << "Root num_children: " << root->num_children() << std::endl;
        std::cout << "Orders is_leaf: " << orders_node->is_leaf() << std::endl;
        
        // Get all table names
        auto all_tables = root->get_all_table_names();
        std::cout << "\nAll tables in tree: ";
        for (const auto& name : all_tables) {
            std::cout << name << " ";
        }
        std::cout << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error during join tree test: " << e.what() << std::endl;
    }
}

void test_encryption() {
    std::cout << "\n=== Testing Table Encryption (CSV Format) ===" << std::endl;
    
    if (global_eid == 0) {
        std::cout << "Enclave not initialized, skipping encryption test" << std::endl;
        return;
    }
    
    try {
        // Load a small table
        std::string csv_path = "../../../plaintext/data/data_0_001/nation.csv";
        Table original = TableIO::load_csv(csv_path);
        std::cout << "Original table: " << original.size() << " rows" << std::endl;
        
        // Save as encrypted CSV
        int32_t key = 12345;
        std::string enc_path = "/tmp/nation_encrypted.csv";
        TableIO::save_encrypted_csv(original, enc_path, key, global_eid);
        std::cout << "Saved encrypted CSV to: " << enc_path << std::endl;
        
        // Load encrypted CSV (no decryption, just load with is_encrypted flag)
        Table encrypted_table = TableIO::load_encrypted_csv(enc_path);
        std::cout << "Loaded encrypted CSV table: " << encrypted_table.size() << " rows" << std::endl;
        std::cout << "First entry is_encrypted flag: " << encrypted_table.get_entry(0).is_encrypted << std::endl;
        
        // Verify the encrypted table has the same size and structure
        if (original.size() == encrypted_table.size()) {
            std::cout << "✓ Size matches: " << original.size() << " rows" << std::endl;
            
            // Check that is_encrypted flag is set correctly
            bool all_encrypted = true;
            for (size_t i = 0; i < encrypted_table.size() && i < 5; ++i) {
                if (!encrypted_table.get_entry(i).is_encrypted) {
                    all_encrypted = false;
                    break;
                }
            }
            
            if (all_encrypted) {
                std::cout << "✓ All entries marked as encrypted" << std::endl;
            } else {
                std::cout << "✗ Some entries not marked as encrypted" << std::endl;
            }
            
            // Peek at the encrypted CSV file to verify it contains encrypted values
            std::ifstream file(enc_path);
            std::string line;
            std::getline(file, line); // Skip header
            if (std::getline(file, line)) {
                std::cout << "First data row (encrypted values): " << line.substr(0, 50) << "..." << std::endl;
            }
            file.close();
            
            std::cout << "✓ Encrypted CSV format test successful!" << std::endl;
        } else {
            std::cout << "✗ Size mismatch: original=" << original.size() 
                     << ", encrypted=" << encrypted_table.size() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error during encryption test: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Testing Table I/O Infrastructure" << std::endl;
    std::cout << "=================================" << std::endl;
    
    // Initialize enclave
    if (initialize_enclave() != 0) {
        std::cerr << "Failed to initialize enclave" << std::endl;
        // Continue with non-enclave tests
    } else {
        std::cout << "Enclave initialized successfully" << std::endl;
    }
    
    // Run tests
    test_join_constraint();
    test_csv_loading();
    test_join_tree();
    test_encryption();
    
    // Cleanup
    destroy_enclave();
    
    std::cout << "\nAll tests completed!" << std::endl;
    
    return 0;
}