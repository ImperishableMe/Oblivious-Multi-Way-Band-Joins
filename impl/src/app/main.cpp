#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <dirent.h>
#include "sgx_urts.h"
#include "Enclave_u.h"
#include "algorithms/oblivious_join.h"
#include "data_structures/join_tree_node.h"
#include "data_structures/join_tree_builder.h"
#include "query/query_parser.h"
#include "../common/debug_util.h"
#include "io/table_io.h"
#include "../common/debug_util.h"

/* Global enclave ID */
sgx_enclave_id_t global_eid = 0;

/* Initialize the enclave */
int initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    /* Call sgx_create_enclave to initialize an enclave instance */
    ret = sgx_create_enclave("/home/r33wei/omwj/memory_const/impl/src/enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: 0x" << std::hex << ret << std::endl;
        return -1;
    }
    
    // Enclave initialized
    return 0;
}

/* Destroy the enclave */
void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
        // Enclave destroyed
    }
}

/* Parse SQL query from file and build join tree */
JoinTreeNodePtr parse_sql_query(const std::string& query_file, 
                                const std::map<std::string, Table>& tables) {
    // Read SQL query from file
    std::ifstream file(query_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open query file: " + query_file);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string sql_query = buffer.str();
    file.close();
    
    // SQL Query loaded
    
    // Parse SQL query
    QueryParser parser;
    ParsedQuery parsed_query = parser.parse(sql_query);
    
    // Query parsed
    
    // Build join tree from parsed query
    JoinTreeBuilder builder;
    JoinTreeNodePtr root = builder.build_from_query(parsed_query, tables);
    
    if (!root) {
        throw std::runtime_error("Failed to build join tree from query");
    }
    
    return root;
}

/* Print usage information */
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <query_file> <input_dir> <output_file>" << std::endl;
    std::cout << "  query_file  : SQL query file (.sql)" << std::endl;
    std::cout << "  input_dir   : Directory containing encrypted CSV table files" << std::endl;
    std::cout << "  output_file : Output file for encrypted join result" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string query_file = argv[1];
    std::string input_dir = argv[2];
    std::string output_file = argv[3];
    
    // Starting SGX oblivious join
    
    try {
        // Initialize the enclave
        if (initialize_enclave() < 0) {
            std::cerr << "Enclave initialization failed!" << std::endl;
            return -1;
        }
        
        // Load all CSV files from input directory
        std::map<std::string, Table> tables;
        
        DIR* dir = opendir(input_dir.c_str());
        if (!dir) {
            throw std::runtime_error("Cannot open input directory: " + input_dir);
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".csv") {
                std::string filepath = input_dir + "/" + filename;
                std::string table_name = filename.substr(0, filename.size() - 4);
                
                Table table = TableIO::load_csv(filepath);
                table.set_table_name(table_name);
                tables[table_name] = table;
            }
        }
        closedir(dir);
        
        if (tables.empty()) {
            throw std::runtime_error("No CSV files found in input directory");
        }
        
        // Parse SQL query and build join tree
        JoinTreeNodePtr join_tree = parse_sql_query(query_file, tables);
        
        // Execute oblivious join with debug output
        Table result = ObliviousJoin::ExecuteWithDebug(join_tree, global_eid, "oblivious_join");
        
        // Save result (encrypted with nonce)
        TableIO::save_encrypted_csv(result, output_file, global_eid);
        printf("Result: %zu rows\n", result.size());
        
        // Cleanup
        destroy_enclave();
        
        // Join complete
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        destroy_enclave();
        return 1;
    }
}