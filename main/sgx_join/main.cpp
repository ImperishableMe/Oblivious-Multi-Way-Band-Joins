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
        // Step 1: Parse SQL query to get table aliases
        std::ifstream file(query_file);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open query file: " + query_file);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string sql_query = buffer.str();
        file.close();

        QueryParser parser;
        ParsedQuery parsed_query = parser.parse(sql_query);

        // Step 2: Load base CSV files from input directory
        std::map<std::string, Table> base_tables;

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
                base_tables.emplace(table_name, std::move(table));
            }
        }
        closedir(dir);

        if (base_tables.empty()) {
            throw std::runtime_error("No CSV files found in input directory");
        }

        // Step 3: Create aliased tables by copying base tables
        std::map<std::string, Table> aliased_tables;

        for (const auto& alias : parsed_query.tables) {
            // Resolve alias to CSV filename
            std::string filename = parsed_query.resolve_table(alias);

            // Find the base table
            auto it = base_tables.find(filename);
            if (it == base_tables.end()) {
                throw std::runtime_error("Table '" + filename + "' (for alias '" + alias +
                                       "') not found in input directory");
            }

            // Create a copy of the table with the alias name
            Table table_copy(it->second);  // Use copy constructor
            table_copy.set_table_name(alias);
            aliased_tables.emplace(alias, std::move(table_copy));
        }

        // Step 4: Build join tree using aliased tables
        JoinTreeBuilder builder;
        JoinTreeNodePtr join_tree = builder.build_from_query(parsed_query, aliased_tables);

        if (!join_tree) {
            throw std::runtime_error("Failed to build join tree from query");
        }

        // Execute oblivious join with debug output
        Table result = ObliviousJoin::ExecuteWithDebug(join_tree, "oblivious_join");

        // Save result
        TableIO::save_csv(result, output_file);
        printf("Result: %zu rows\n", result.size());

        // Join complete
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}