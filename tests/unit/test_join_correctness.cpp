/**
 * Test join correctness using SQLite as ground truth
 */

#include <iostream>
#include <fstream>
#include <cassert>
#include <map>
#include <filesystem>
#include "../../src/query/query_parser.h"
#include "../../src/core/join_tree_builder.h"
#include "test_utils/simple_join_executor.h"
#include "test_utils/sqlite_ground_truth.h"
#include "test_utils/join_result_comparator.h"
#include "../../src/io/table_io.h"
#include "../../src/crypto/crypto_utils.h"
#include "Enclave_u.h"
#include "sgx_urts.h"

namespace fs = std::filesystem;

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

class JoinCorrectnessTest {
private:
    std::string data_dir;
    std::string query_dir;
    bool verbose;
    
    /**
     * Load table from CSV file
     * Automatically detects and decrypts if needed
     */
    Table load_table_from_csv(const std::string& filename) {
        std::string filepath = data_dir + "/" + filename + ".csv";
        
        if (!fs::exists(filepath)) {
            throw std::runtime_error("CSV file not found: " + filepath);
        }
        
        Table table = TableIO::load_csv(filepath);
        
        // Decrypt if needed using real SGX decryption
        if (table.get_encryption_status() == Table::ENCRYPTED) {
            if (global_eid == 0) {
                throw std::runtime_error("Enclave not initialized for encrypted table");
            }
            
            for (size_t i = 0; i < table.size(); i++) {
                Entry& entry = table[i];
                if (entry.get_is_encrypted()) {
                    crypto_status_t status = CryptoUtils::decrypt_entry(entry, global_eid);
                    if (status != CRYPTO_SUCCESS) {
                        throw std::runtime_error("Failed to decrypt entry " + std::to_string(i));
                    }
                }
            }
        }
        
        return table;
    }
    
    /**
     * Load query from SQL file
     */
    std::string load_query_from_file(const std::string& filename) {
        std::string filepath = query_dir + "/" + filename;
        
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Query file not found: " + filepath);
        }
        
        std::string query((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        
        return query;
    }
    
public:
    JoinCorrectnessTest(const std::string& data, const std::string& query, bool v = false) 
        : data_dir(data), query_dir(query), verbose(v) {}
    
    /**
     * Test a single query
     */
    bool test_query(const std::string& query_file, const std::string& test_name) {
        std::cout << "\n=== Testing: " << test_name << " ===" << std::endl;
        
        try {
            // 1. Load and parse query
            std::string sql = load_query_from_file(query_file);
            if (verbose) {
                std::cout << "Query: " << sql << std::endl;
            }
            
            QueryParser parser;
            ParsedQuery parsed = parser.parse(sql);
            
            std::cout << "  Tables: " << parsed.num_tables() 
                     << ", Joins: " << parsed.num_joins() << std::endl;
            
            // 2. Load tables
            std::map<std::string, Table> tables;
            for (const auto& table_name : parsed.tables) {
                if (verbose) {
                    std::cout << "  Loading table: " << table_name << std::endl;
                }
                tables[table_name] = load_table_from_csv(table_name);
                std::cout << "    " << table_name << ": " 
                         << tables[table_name].size() << " rows" << std::endl;
            }
            
            // 3. Build join tree
            JoinTreeBuilder tree_builder;
            auto join_tree = tree_builder.build_from_query(parsed, tables);
            
            if (verbose) {
                std::cout << "\nJoin Tree Structure:" << std::endl;
                join_tree->print_tree();
            }
            
            // 4. Execute with SimpleJoinExecutor
            SimpleJoinExecutor executor(global_eid);
            Table our_result = executor.execute_join_tree(join_tree);
            
            std::cout << "\n  Our result: " << our_result.size() << " rows" << std::endl;
            
            // 5. Execute with SQLite ground truth
            SQLiteGroundTruth sqlite;
            sqlite.open_database();
            
            // Load tables into SQLite
            for (const auto& [name, table] : tables) {
                sqlite.load_table(name, table);
            }
            
            // Execute query
            Table sqlite_result = sqlite.execute_query(sql);
            std::cout << "  SQLite result: " << sqlite_result.size() << " rows" << std::endl;
            
            sqlite.close_database();
            
            // 6. Compare results
            JoinResultComparator comparator;
            bool equivalent = comparator.are_equivalent(our_result, sqlite_result);
            
            if (equivalent) {
                std::cout << "  ✓ Results are EQUIVALENT!" << std::endl;
            } else {
                std::cout << "  ✗ Results differ!" << std::endl;
                if (verbose) {
                    std::cout << comparator.generate_report(our_result, sqlite_result);
                } else {
                    for (const auto& diff : comparator.get_differences()) {
                        std::cout << "    " << diff << std::endl;
                    }
                }
            }
            
            return equivalent;
            
        } catch (const std::exception& e) {
            std::cout << "  ✗ Test failed with error: " << e.what() << std::endl;
            return false;
        }
    }
    
    /**
     * Run all TPC-H query tests
     */
    void run_all_tests() {
        std::cout << "Join Correctness Test Suite" << std::endl;
        std::cout << "===========================" << std::endl;
        std::cout << "Data directory: " << data_dir << std::endl;
        std::cout << "Query directory: " << query_dir << std::endl;
        
        struct TestCase {
            std::string query_file;
            std::string name;
            std::string description;
        };
        
        std::vector<TestCase> test_cases = {
            {"tpch_tm1.sql", "TM1", "3-table equality joins (customer-orders-lineitem)"},
            {"tpch_tm2.sql", "TM2", "4-table equality joins (supplier-customer-nation)"},
            {"tpch_tm3.sql", "TM3", "5-table equality joins (nation-supplier-customer-orders-lineitem)"},
            {"tpch_tb1.sql", "TB1", "2-table band join (supplier account balance)"},
            {"tpch_tb2.sql", "TB2", "2-table band join (part retail price)"}
        };
        
        int passed = 0;
        int total = 0;
        
        for (const auto& test : test_cases) {
            std::cout << "\n" << test.name << ": " << test.description << std::endl;
            
            bool success = test_query(test.query_file, test.name);
            
            if (success) {
                passed++;
            }
            total++;
        }
        
        // Summary
        std::cout << "\n=== Test Summary ===" << std::endl;
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
    std::cout << "  -d <dir>  Data directory (default: ../../../plaintext/data/data_0_001)" << std::endl;
    std::cout << "  -q <dir>  Query directory (default: ../../../queries)" << std::endl;
    std::cout << "  -v        Verbose output" << std::endl;
    std::cout << "  -h        Show this help" << std::endl;
}

int main(int argc, char** argv) {
    // Default paths
    std::string data_dir = "../../../plaintext/data/data_0_001";
    std::string query_dir = "../../../queries";
    bool verbose = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-d" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "-q" && i + 1 < argc) {
            query_dir = argv[++i];
        } else if (arg == "-v") {
            verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Initialize SGX enclave
    if (initialize_enclave() < 0) {
        std::cerr << "Failed to initialize SGX enclave" << std::endl;
        return 1;
    }
    
    int result = 0;
    try {
        JoinCorrectnessTest tester(data_dir, query_dir, verbose);
        tester.run_all_tests();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        result = 1;
    }
    
    // Destroy enclave
    destroy_enclave();
    
    return result;
}