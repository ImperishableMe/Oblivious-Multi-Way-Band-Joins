#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <dirent.h>
#include "algorithms/oblivious_join.h"
#include "join/join_tree_node.h"
#include "join/join_tree_builder.h"
#include "query/query_parser.h"
#include "debug_util.h"
#include "file_io/table_io.h"
#include "batch/ecall_wrapper.h"

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
    std::cout << "  input_dir   : Directory containing CSV table files" << std::endl;
    std::cout << "  output_file : Output file for join result" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string query_file = argv[1];
    std::string input_dir = argv[2];
    std::string output_file = argv[3];

    // Starting TDX oblivious join

    try {
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
                tables.emplace(table_name, std::move(table));
            }
        }
        closedir(dir);
        
        if (tables.empty()) {
            throw std::runtime_error("No CSV files found in input directory");
        }
        
        // Parse SQL query and build join tree
        JoinTreeNodePtr join_tree = parse_sql_query(query_file, tables);

        // Reset counters before execution
        reset_ecall_count();
        reset_ocall_count();

        // Execute oblivious join with debug output
        Table result = ObliviousJoin::ExecuteWithDebug(join_tree, "oblivious_join");

        // Save result
        TableIO::save_csv(result, output_file);
        printf("Result: %zu rows\n", result.size());

        // Output operation counts in parseable format
        printf("OPERATION_COUNT: %zu\n", get_ecall_count());
        printf("CALLBACK_COUNT: %zu\n", get_ocall_count());

        // Join complete
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}