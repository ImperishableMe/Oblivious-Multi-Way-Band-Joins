/**
 * Test for Bottom-Up Phase of Oblivious Multi-Way Band Join
 * 
 * Verifies that the bottom-up phase correctly computes local multiplicities
 * by comparing against ground truth from SimpleJoinExecutor.
 * 
 * Tests with real TPC-H data and queries (TB1, TB2, TM1, TM2, TM3)
 */

#include <iostream>
#include <fstream>
#include <chrono>
#include <map>
#include <cassert>
#include "../../app/algorithms/bottom_up_phase.h"
#include "../../app/query/query_parser.h"
#include "../../app/data_structures/join_tree_builder.h"
#include "../../app/io/table_io.h"
#include "../../app/crypto/crypto_utils.h"
#include "../../app/Enclave_u.h"
#include "../utils/subtree_verifier.h"
#include "../../common/debug_util.h"
#include "sgx_urts.h"

sgx_enclave_id_t global_eid = 0;

int initialize_enclave() {
    sgx_status_t ret = sgx_create_enclave("../enclave.signed.so", SGX_DEBUG_FLAG, 
                                          NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return -1;
    }
    std::cout << "SGX Enclave initialized successfully (ID: " << global_eid << ")" << std::endl;
    return 0;
}

void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
        std::cout << "SGX Enclave destroyed" << std::endl;
    }
}

class BottomUpTest {
private:
    std::string data_dir;
    std::string query_dir;
    bool use_encrypted;
    
    /**
     * Load SQL query from file
     */
    std::string LoadQuery(const std::string& filename) {
        std::string filepath = query_dir + "/" + filename;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open query file: " + filepath);
        }
        return std::string((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    }
    
    /**
     * Load and optionally decrypt table
     */
    Table LoadTable(const std::string& name) {
        std::string filepath = data_dir + "/" + name + ".csv";
        Table table = TableIO::load_csv(filepath);
        
        // If encrypted, tables will be decrypted during processing
        // by the test infrastructure (SimpleJoinExecutor)
        
        std::cout << "  Loaded " << name << ": " << table.size() << " rows";
        if (table.get_encryption_status() == Table::ENCRYPTED) {
            std::cout << " (encrypted)";
        }
        std::cout << std::endl;
        
        return table;
    }
    
public:
    BottomUpTest(bool encrypted = false) : use_encrypted(encrypted) {
        if (use_encrypted) {
            data_dir = "/home/r33wei/omwj/memory_const_public/encrypted/data_0_001";
        } else {
            data_dir = "/home/r33wei/omwj/memory_const_public/plaintext/data_0_001";
        }
        query_dir = "/home/r33wei/omwj/memory_const_public/queries";
    }
    
    BottomUpTest(const std::string& query_path, const std::string& data_path)
        : use_encrypted(false), query_dir(query_path), data_dir(data_path) {
    }
    
    /**
     * Test custom query and data
     */
    bool TestCustom(const std::string& query_file, const std::string& data_path) {
        std::cout << "\n=== Testing Custom Query ===" << std::endl;
        std::cout << "Query file: " << query_file << std::endl;
        std::cout << "Data path: " << data_path << std::endl;
        
        // Extract test name from query file for debug session
        size_t last_slash = query_file.find_last_of("/");
        size_t last_dot = query_file.find_last_of(".");
        std::string test_name = "custom_test";
        if (last_slash != std::string::npos && last_dot != std::string::npos) {
            test_name = query_file.substr(last_slash + 1, last_dot - last_slash - 1);
        }
        
        // Initialize debug session
        debug_init_session(test_name.c_str());
        
        try {
            // Load the query directly (it's already a full path)
            std::ifstream file(query_file);
            if (!file.is_open()) {
                throw std::runtime_error("Cannot open query file: " + query_file);
            }
            std::string query((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
            
            // Parse query to build join tree
            QueryParser parser;
            ParsedQuery parsed = parser.parse(query);
            
            // Load tables based on query
            std::map<std::string, Table> tables;
            data_dir = data_path; // Update data_dir for LoadTable
            for (const auto& table_name : parsed.tables) {
                tables[table_name] = LoadTable(table_name);
            }
            
            // Build the join tree
            JoinTreeBuilder builder;
            auto root = builder.build_from_query(parsed, tables);
            
            // Run bottom-up phase
            BottomUpPhase::Execute(root, global_eid);
            
            // Verify results
            bool success = SubtreeVerifier::VerifyFullTree(root, global_eid);
            
            if (success) {
                std::cout << "✓ Test passed" << std::endl;
            } else {
                std::cout << "✗ Test failed: multiplicities don't match ground truth" << std::endl;
            }
            
            // Close debug session
            debug_close_session();
            
            return success;
            
        } catch (const std::exception& e) {
            std::cout << "✗ Test failed: " << e.what() << std::endl;
            debug_close_session();
            return false;
        }
    }
    
    /**
     * Test TB1: 2-table band join (simplest case)
     */
    bool TestTB1() {
        std::cout << "\n=== Testing TB1: 2-Table Band Join ===" << std::endl;
        std::cout << "Query: supplier1 JOIN supplier2 ON balance range" << std::endl;
        
        // Initialize debug session for this test
        debug_init_session("TB1_test");
        
        try {
            // Load tables
            Table supplier1 = LoadTable("supplier1");
            Table supplier2 = LoadTable("supplier2");
            
            // Parse query
            QueryParser parser;
            std::string sql = LoadQuery("tpch_tb1.sql");
            ParsedQuery parsed = parser.parse(sql);
            
            // Build join tree
            std::map<std::string, Table> tables;
            tables["supplier1"] = supplier1;
            tables["supplier2"] = supplier2;
            
            JoinTreeBuilder builder;
            auto root = builder.build_from_query(parsed, tables);
            
            std::cout << "\nJoin tree structure:" << std::endl;
            root->print_tree();
            
            // Run bottom-up phase
            std::cout << "\nRunning bottom-up phase..." << std::endl;
            auto start = std::chrono::high_resolution_clock::now();
            
            BottomUpPhase::Execute(root, global_eid);
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "Bottom-up phase completed in " << duration.count() << " ms" << std::endl;
            
            // Verify results
            std::cout << "\nVerifying multiplicities..." << std::endl;
            bool success = SubtreeVerifier::VerifyFullTree(root, global_eid);
            
            std::cout << "\nTB1 Result: " << (success ? "PASSED ✓" : "FAILED ✗") << std::endl;
            
            // Close debug session
            debug_close_session();
            
            return success;
            
        } catch (const std::exception& e) {
            std::cerr << "TB1 test failed: " << e.what() << std::endl;
            debug_close_session();
            return false;
        }
    }
    
    /**
     * Test TM1: 3-table equality join
     */
    bool TestTM1() {
        std::cout << "\n=== Testing TM1: 3-Table Equality Join ===" << std::endl;
        std::cout << "Query: customer JOIN orders JOIN lineitem" << std::endl;
        
        try {
            // Load tables
            Table customer = LoadTable("customer");
            Table orders = LoadTable("orders");
            Table lineitem = LoadTable("lineitem");
            
            // Parse query
            QueryParser parser;
            std::string sql = LoadQuery("tpch_tm1.sql");
            ParsedQuery parsed = parser.parse(sql);
            
            // Build join tree
            std::map<std::string, Table> tables;
            tables["customer"] = customer;
            tables["orders"] = orders;
            tables["lineitem"] = lineitem;
            
            JoinTreeBuilder builder;
            auto root = builder.build_from_query(parsed, tables);
            
            std::cout << "\nJoin tree structure:" << std::endl;
            root->print_tree();
            
            // Run bottom-up phase
            std::cout << "\nRunning bottom-up phase..." << std::endl;
            auto start = std::chrono::high_resolution_clock::now();
            
            BottomUpPhase::Execute(root, global_eid);
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "Bottom-up phase completed in " << duration.count() << " ms" << std::endl;
            
            // Verify results
            std::cout << "\nVerifying multiplicities..." << std::endl;
            bool success = SubtreeVerifier::VerifyFullTree(root, global_eid);
            
            std::cout << "\nTM1 Result: " << (success ? "PASSED ✓" : "FAILED ✗") << std::endl;
            return success;
            
        } catch (const std::exception& e) {
            std::cerr << "TM1 test failed: " << e.what() << std::endl;
            return false;
        }
    }
    
    /**
     * Test all TPC-H queries
     */
    void TestAllQueries() {
        struct TestCase {
            std::string query_file;
            std::string name;
            std::vector<std::string> table_names;
            std::string description;
        };
        
        std::vector<TestCase> test_cases = {
            {"tpch_tb1.sql", "TB1", {"supplier1", "supplier2"}, 
             "2-table band join (account balance)"},
            {"tpch_tb2.sql", "TB2", {"part1", "part2"},
             "2-table band join (retail price)"},
            {"tpch_tm1.sql", "TM1", {"customer", "orders", "lineitem"},
             "3-table equality join chain"},
            {"tpch_tm2.sql", "TM2", {"supplier", "customer", "nation1", "nation2"},
             "4-table equality joins"},
            {"tpch_tm3.sql", "TM3", {"nation", "supplier", "customer", "orders", "lineitem"},
             "5-table equality joins"}
        };
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "Running Full Test Suite" << std::endl;
        std::cout << "Data: " << (use_encrypted ? "ENCRYPTED" : "PLAINTEXT") << std::endl;
        std::cout << "========================================" << std::endl;
        
        int passed = 0;
        int total = test_cases.size();
        
        for (const auto& test : test_cases) {
            std::cout << "\n=== " << test.name << ": " << test.description << " ===" << std::endl;
            
            // Initialize debug session for this test
            debug_init_session(test.name.c_str());
            
            try {
                // Load tables
                std::map<std::string, Table> tables;
                for (const auto& table_name : test.table_names) {
                    tables[table_name] = LoadTable(table_name);
                }
                
                // Parse query
                QueryParser parser;
                std::string sql = LoadQuery(test.query_file);
                ParsedQuery parsed = parser.parse(sql);
                
                // Build join tree
                JoinTreeBuilder builder;
                auto root = builder.build_from_query(parsed, tables);
                
                // Run bottom-up phase
                auto start = std::chrono::high_resolution_clock::now();
                BottomUpPhase::Execute(root, global_eid);
                auto end = std::chrono::high_resolution_clock::now();
                
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                std::cout << "Bottom-up phase: " << duration.count() << " ms" << std::endl;
                
                // Verify
                bool success = SubtreeVerifier::VerifyFullTree(root, global_eid);
                
                if (success) {
                    std::cout << test.name << ": PASSED ✓" << std::endl;
                    passed++;
                } else {
                    std::cout << test.name << ": FAILED ✗" << std::endl;
                }
                
                // Close debug session
                debug_close_session();
                
            } catch (const std::exception& e) {
                std::cerr << test.name << " failed with error: " << e.what() << std::endl;
                debug_close_session();
            }
        }
        
        // Summary
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Summary" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Passed: " << passed << "/" << total << std::endl;
        
        if (passed == total) {
            std::cout << "✓ ALL TESTS PASSED!" << std::endl;
        } else {
            std::cout << "✗ Some tests failed" << std::endl;
        }
    }
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -e                Use encrypted data" << std::endl;
    std::cout << "  -q <query_file>   Run specific query file" << std::endl;
    std::cout << "  -d <data_dir>     Data directory (use with -q)" << std::endl;
    std::cout << "  --quick           Quick test (TB1 only)" << std::endl;
    std::cout << "  -h                Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << "                    # Run full TPC-H test suite" << std::endl;
    std::cout << "  " << program << " --quick             # Run TB1 only" << std::endl;
    std::cout << "  " << program << " -q query.sql -d data/  # Run custom query with data" << std::endl;
}

int main(int argc, char** argv) {
    bool use_encrypted = false;
    bool quick_test = false;
    std::string query_file;
    std::string data_dir;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-e") {
            use_encrypted = true;
        } else if (arg == "-q" && i + 1 < argc) {
            query_file = argv[++i];
        } else if (arg == "-d" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--quick") {
            quick_test = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    // Initialize enclave
    if (initialize_enclave() < 0) {
        std::cerr << "Failed to initialize SGX enclave" << std::endl;
        return 1;
    }
    
    try {
        // Custom query mode
        if (!query_file.empty()) {
            if (data_dir.empty()) {
                std::cerr << "Error: -d <data_dir> required when using -q <query_file>" << std::endl;
                print_usage(argv[0]);
                destroy_enclave();
                return 1;
            }
            
            BottomUpTest test("", ""); // Dummy constructor, will be overridden
            bool success = test.TestCustom(query_file, data_dir);
            destroy_enclave();
            return success ? 0 : 1;
        }
        
        // Standard TPC-H test mode
        BottomUpTest test(use_encrypted);
        
        if (quick_test) {
            // Just run TB1 for quick verification
            bool success = test.TestTB1();
            destroy_enclave();
            return success ? 0 : 1;
        } else {
            // Run full test suite
            test.TestAllQueries();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        destroy_enclave();
        return 1;
    }
    
    destroy_enclave();
    return 0;
}