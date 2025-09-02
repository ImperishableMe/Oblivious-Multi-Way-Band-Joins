/**
 * Test full join pipeline including:
 * 1. Query parsing
 * 2. Join tree building
 * 3. Bottom-up phase (compute local multiplicities)
 * 4. Top-down phase (compute final multiplicities)
 * 5. Actual join execution
 * 6. Verification that final_mult matches actual result cardinalities
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include "../../app/algorithms/bottom_up_phase.h"
#include "../../app/algorithms/top_down_phase.h"
#include "../../app/query/query_parser.h"
#include "../../app/data_structures/join_tree_builder.h"
#include "../../app/io/table_io.h"
#include "../../app/Enclave_u.h"
#include "../../common/debug_util.h"
#include "../utils/simple_join_executor.h"
#include "sgx_urts.h"

namespace fs = std::filesystem;

class FullJoinTest {
private:
    sgx_enclave_id_t eid;
    std::string query_path;
    std::string data_path;
    bool debug_mode;
    
public:
    FullJoinTest(sgx_enclave_id_t enclave_id, bool debug = false) 
        : eid(enclave_id), debug_mode(debug) {}
    
    bool RunTest(const std::string& query_file, const std::string& data_dir) {
        query_path = query_file;
        data_path = data_dir;
        
        std::cout << "\n=== Full Join Pipeline Test ===" << std::endl;
        std::cout << "Query: " << query_file << std::endl;
        std::cout << "Data: " << data_dir << std::endl;
        
        try {
            // Step 1: Parse query
            std::cout << "\n--- Step 1: Parse Query ---" << std::endl;
            // Read SQL file content
            std::ifstream query_stream(query_file);
            if (!query_stream.is_open()) {
                throw std::runtime_error("Cannot open query file: " + query_file);
            }
            std::string sql_query((std::istreambuf_iterator<char>(query_stream)),
                                  std::istreambuf_iterator<char>());
            query_stream.close();
            
            QueryParser parser;
            auto query = parser.parse(sql_query);
            std::cout << "  Tables: " << query.tables.size() << std::endl;
            std::cout << "  Join conditions: " << query.join_conditions.size() << std::endl;
            
            // Step 2: Build join tree
            std::cout << "\n--- Step 2: Build Join Tree ---" << std::endl;
            
            // First load tables to get the map
            std::map<std::string, Table> tables_map;
            for (const auto& table_name : query.tables) {
                std::string table_file = data_dir + "/" + table_name + ".csv";
                Table table = TableIO::load_csv(table_file);
                table.set_table_name(table_name);
                tables_map[table_name] = table;
            }
            
            JoinTreeBuilder builder;
            auto root = builder.build_from_query(query, tables_map);
            std::cout << "  Root table: " << root->get_table_name() << std::endl;
            
            // Tables already loaded in the tree during build_from_query
            
            // Initialize debug session
            std::string test_name = fs::path(query_file).stem();
            if (debug_mode) {
                debug_init_session(test_name.c_str());
            }
            
            // Step 4: Bottom-up phase
            std::cout << "\n--- Step 4: Bottom-Up Phase ---" << std::endl;
            BottomUpPhase::Execute(root, eid);
            PrintLocalMultiplicities(root);
            
            // Step 5: Top-down phase
            std::cout << "\n--- Step 5: Top-Down Phase ---" << std::endl;
            TopDownPhase::Execute(root, eid);
            PrintFinalMultiplicities(root);
            
            // Step 6: Execute actual join
            std::cout << "\n--- Step 6: Execute Actual Join ---" << std::endl;
            SimpleJoinExecutor executor;
            auto join_result = executor.execute_join_tree(root);
            std::cout << "  Join result: " << join_result.size() << " tuples" << std::endl;
            
            // Step 7: Verify multiplicities
            std::cout << "\n--- Step 7: Verify Multiplicities ---" << std::endl;
            // Convert Table to vector<Entry> for verification
            std::vector<Entry> result_entries;
            for (size_t i = 0; i < join_result.size(); i++) {
                result_entries.push_back(join_result[i]);
            }
            bool success = VerifyMultiplicities(root, result_entries);
            
            if (debug_mode) {
                debug_close_session();
            }
            
            return success;
            
        } catch (const std::exception& e) {
            std::cerr << "Test failed: " << e.what() << std::endl;
            if (debug_mode) {
                debug_close_session();
            }
            return false;
        }
    }
    
private:
    void PrintLocalMultiplicities(JoinTreeNodePtr node, int depth = 0) {
        std::string indent(depth * 2, ' ');
        const Table& table = node->get_table();
        
        std::cout << indent << node->get_table_name() << ": [";
        for (size_t i = 0; i < table.size() && i < 10; i++) {
            if (i > 0) std::cout << ", ";
            std::cout << table[i].local_mult;
        }
        if (table.size() > 10) std::cout << ", ...";
        std::cout << "]" << std::endl;
        
        for (auto& child : node->get_children()) {
            PrintLocalMultiplicities(child, depth + 1);
        }
    }
    
    void PrintFinalMultiplicities(JoinTreeNodePtr node, int depth = 0) {
        std::string indent(depth * 2, ' ');
        const Table& table = node->get_table();
        
        std::cout << indent << node->get_table_name() << " final: [";
        for (size_t i = 0; i < table.size() && i < 10; i++) {
            if (i > 0) std::cout << ", ";
            std::cout << table[i].final_mult;
        }
        if (table.size() > 10) std::cout << ", ...";
        std::cout << "]" << std::endl;
        
        for (auto& child : node->get_children()) {
            PrintFinalMultiplicities(child, depth + 1);
        }
    }
    
    bool VerifyMultiplicities(JoinTreeNodePtr node, const std::vector<Entry>& join_result) {
        // Count occurrences of each tuple in the join result
        std::map<std::string, std::map<int, int>> tuple_counts;
        
        // Build tuple counts from join result
        for (const auto& result_entry : join_result) {
            // For each table column in the result
            for (size_t i = 0; i < result_entry.column_names.size(); i++) {
                std::string col_name = result_entry.column_names[i];
                
                // Extract table name from column name (e.g., "l_id" -> "left_table")
                std::string table_name = ExtractTableName(node, col_name);
                if (!table_name.empty()) {
                    int value = result_entry.attributes[i];
                    tuple_counts[table_name][value]++;
                }
            }
        }
        
        // Verify each table's multiplicities
        bool all_correct = true;
        all_correct &= VerifyTableMultiplicities(node, tuple_counts);
        
        return all_correct;
    }
    
    bool VerifyTableMultiplicities(JoinTreeNodePtr node, 
                                   const std::map<std::string, std::map<int, int>>& tuple_counts) {
        const Table& table = node->get_table();
        std::string table_name = node->get_table_name();
        bool correct = true;
        
        std::cout << "  " << table_name << ":" << std::endl;
        
        for (size_t i = 0; i < table.size(); i++) {
            const Entry& entry = table[i];
            
            // Get the primary key value (first attribute)
            int key_value = entry.attributes.empty() ? 0 : entry.attributes[0];
            
            // Get actual count from join result
            int actual_count = 0;
            auto it = tuple_counts.find(table_name);
            if (it != tuple_counts.end()) {
                auto count_it = it->second.find(key_value);
                if (count_it != it->second.end()) {
                    actual_count = count_it->second;
                }
            }
            
            // Compare with final_mult
            bool match = (entry.final_mult == static_cast<uint32_t>(actual_count));
            
            std::cout << "    Row " << i << " (key=" << key_value << "): "
                      << "final_mult=" << entry.final_mult 
                      << ", actual=" << actual_count
                      << (match ? " ✓" : " ✗") << std::endl;
            
            if (!match) correct = false;
        }
        
        // Recursively verify children
        for (auto& child : node->get_children()) {
            correct &= VerifyTableMultiplicities(child, tuple_counts);
        }
        
        return correct;
    }
    
    std::string ExtractTableName(JoinTreeNodePtr root, const std::string& column_name) {
        // Simple heuristic: match column prefixes with table names
        return FindTableWithColumn(root, column_name);
    }
    
    std::string FindTableWithColumn(JoinTreeNodePtr node, const std::string& column_name) {
        const Table& table = node->get_table();
        
        // Check if this table has the column
        if (table.size() > 0) {
            for (const auto& col : table[0].column_names) {
                if (col == column_name) {
                    return node->get_table_name();
                }
            }
        }
        
        // Check children
        for (auto& child : node->get_children()) {
            std::string result = FindTableWithColumn(child, column_name);
            if (!result.empty()) return result;
        }
        
        return "";
    }
};

// Main test runner
int main(int argc, char* argv[]) {
    // Parse command line arguments
    bool debug = false;
    std::string query_file;
    std::string data_dir;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" || arg == "--debug") {
            debug = true;
        } else if (arg == "-q" && i + 1 < argc) {
            query_file = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -q <query_file>  SQL query file to test" << std::endl;
            std::cout << "  -p <data_path>   Path to data directory" << std::endl;
            std::cout << "  -d, --debug      Enable debug output" << std::endl;
            std::cout << "  -h, --help       Show this help" << std::endl;
            return 0;
        }
    }
    
    // Default test cases if no query specified
    if (query_file.empty()) {
        query_file = "../../../test_cases/queries/two_table_basic.sql";
        data_dir = "../../../test_cases/plaintext/";
    }
    
    // Initialize SGX enclave
    sgx_enclave_id_t eid;
    sgx_status_t status = sgx_create_enclave(
        "enclave.signed.so",
        SGX_DEBUG_FLAG,
        NULL,
        NULL,
        &eid,
        NULL
    );
    
    if (status != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: 0x" << std::hex << status << std::endl;
        return 1;
    }
    
    std::cout << "SGX Enclave initialized successfully (ID: " << eid << ")" << std::endl;
    
    // Run test
    FullJoinTest test(eid, debug);
    bool success = test.RunTest(query_file, data_dir);
    
    // Cleanup
    sgx_destroy_enclave(eid);
    std::cout << "SGX Enclave destroyed" << std::endl;
    
    if (success) {
        std::cout << "\n✓ Test passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ Test failed!" << std::endl;
        return 1;
    }
}