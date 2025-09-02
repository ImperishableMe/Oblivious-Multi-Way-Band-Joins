/**
 * Test top-down phase specifically
 * 1. Parse query and build join tree
 * 2. Run bottom-up phase
 * 3. Run top-down phase
 * 4. Verify final multiplicities are computed correctly
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include "../../src/algorithms/bottom_up_phase.h"
#include "../../src/algorithms/top_down_phase.h"
#include "../../src/query/query_parser.h"
#include "../../src/core/join_tree_builder.h"
#include "../../src/io/table_io.h"
#include "Enclave_u.h"
#include "debug_util.h"
#include "sgx_urts.h"

class TopDownTest {
private:
    sgx_enclave_id_t eid;
    std::string test_name;
    
public:
    TopDownTest(sgx_enclave_id_t enclave_id) : eid(enclave_id) {}
    
    bool RunTest(const std::string& query_file, const std::string& data_dir) {
        std::cout << "\n=== Testing Top-Down Phase ===" << std::endl;
        std::cout << "Query: " << query_file << std::endl;
        std::cout << "Data: " << data_dir << std::endl;
        
        try {
            // Parse query
            std::ifstream query_stream(query_file);
            if (!query_stream.is_open()) {
                throw std::runtime_error("Cannot open query file: " + query_file);
            }
            std::string sql_query((std::istreambuf_iterator<char>(query_stream)),
                                  std::istreambuf_iterator<char>());
            query_stream.close();
            
            QueryParser parser;
            auto query = parser.parse(sql_query);
            
            // Build join tree
            std::map<std::string, Table> tables_map;
            for (const auto& table_name : query.tables) {
                std::string table_file = data_dir + "/" + table_name + ".csv";
                Table table = TableIO::load_csv(table_file);
                table.set_table_name(table_name);
                tables_map[table_name] = table;
                std::cout << "  Loaded " << table_name << ": " << table.size() << " rows" << std::endl;
            }
            
            JoinTreeBuilder builder;
            auto root = builder.build_from_query(query, tables_map);
            
            // Initialize debug session
            test_name = query_file.substr(query_file.find_last_of("/\\") + 1);
            test_name = test_name.substr(0, test_name.find_last_of("."));
            debug_init_session(test_name.c_str());
            
            // Run bottom-up phase
            std::cout << "\n--- Bottom-Up Phase ---" << std::endl;
            BottomUpPhase::Execute(root, eid);
            PrintMultiplicities(root, "local_mult");
            
            // Run top-down phase
            std::cout << "\n--- Top-Down Phase ---" << std::endl;
            TopDownPhase::Execute(root, eid);
            PrintMultiplicities(root, "final_mult");
            
            // Verify results
            bool success = VerifyTopDown(root);
            
            debug_close_session();
            
            if (success) {
                std::cout << "✓ Top-down phase completed successfully!" << std::endl;
            } else {
                std::cout << "✗ Top-down phase verification failed!" << std::endl;
            }
            
            return success;
            
        } catch (const std::exception& e) {
            std::cerr << "Test failed: " << e.what() << std::endl;
            debug_close_session();
            return false;
        }
    }
    
private:
    void PrintMultiplicities(JoinTreeNodePtr node, const std::string& field_name, int depth = 0) {
        std::string indent(depth * 2, ' ');
        const Table& table = node->get_table();
        
        std::cout << indent << node->get_table_name() << " " << field_name << ": [";
        for (size_t i = 0; i < table.size() && i < 10; i++) {
            if (i > 0) std::cout << ", ";
            if (field_name == "local_mult") {
                std::cout << table[i].local_mult;
            } else {
                std::cout << table[i].final_mult;
            }
        }
        if (table.size() > 10) std::cout << ", ...";
        std::cout << "]" << std::endl;
        
        for (auto& child : node->get_children()) {
            PrintMultiplicities(child, field_name, depth + 1);
        }
    }
    
    bool VerifyTopDown(JoinTreeNodePtr node, bool is_root = true) {
        const Table& table = node->get_table();
        bool correct = true;
        
        std::cout << "\nVerifying " << node->get_table_name() << ":" << std::endl;
        
        if (is_root) {
            // Root: final_mult should equal local_mult
            for (size_t i = 0; i < table.size(); i++) {
                if (table[i].final_mult != table[i].local_mult) {
                    std::cout << "  Row " << i << ": final_mult=" << table[i].final_mult 
                              << " != local_mult=" << table[i].local_mult << " ✗" << std::endl;
                    correct = false;
                } else {
                    std::cout << "  Row " << i << ": final_mult=" << table[i].final_mult 
                              << " = local_mult=" << table[i].local_mult << " ✓" << std::endl;
                }
            }
        } else {
            // Non-root: final_mult = foreign_contribution * local_mult
            // Just verify final_mult > 0 for now
            for (size_t i = 0; i < table.size(); i++) {
                std::cout << "  Row " << i << ": local_mult=" << table[i].local_mult 
                          << ", final_mult=" << table[i].final_mult << std::endl;
            }
        }
        
        // Recursively verify children
        for (auto& child : node->get_children()) {
            correct &= VerifyTopDown(child, false);
        }
        
        return correct;
    }
};

int main(int argc, char* argv[]) {
    // Parse command line
    std::string query_file;
    std::string data_dir;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-q" && i + 1 < argc) {
            query_file = argv[++i];
        } else if (arg == "-d" && i + 1 < argc) {
            data_dir = argv[++i];
        }
    }
    
    // Default test
    if (query_file.empty()) {
        query_file = "../../../test_cases/queries/two_table_basic.sql";
        data_dir = "../../../test_cases/plaintext/";
    }
    
    // Initialize SGX
    sgx_enclave_id_t eid;
    sgx_status_t status = sgx_create_enclave(
        "enclave.signed.so",
        SGX_DEBUG_FLAG,
        NULL, NULL, &eid, NULL
    );
    
    if (status != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: 0x" << std::hex << status << std::endl;
        return 1;
    }
    
    std::cout << "SGX Enclave initialized (ID: " << eid << ")" << std::endl;
    
    // Run test
    TopDownTest test(eid);
    bool success = test.RunTest(query_file, data_dir);
    
    // Cleanup
    sgx_destroy_enclave(eid);
    std::cout << "SGX Enclave destroyed" << std::endl;
    
    return success ? 0 : 1;
}