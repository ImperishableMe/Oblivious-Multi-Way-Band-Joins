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
#include "query/filter_condition.h"
#include "debug_util.h"
#include "file_io/table_io.h"

/* Print usage information */
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <query_file> <input_dir> <output_file> [--no-filter]" << std::endl;
    std::cout << "  query_file  : SQL query file (.sql)" << std::endl;
    std::cout << "  input_dir   : Directory containing CSV table files" << std::endl;
    std::cout << "  output_file : Output file for join result" << std::endl;
    std::cout << "  --no-filter : Disable WHERE-clause selection pushdown; compute the" << std::endl;
    std::cout << "                full unfiltered multi-way join (output may explode)" << std::endl;
}

int main(int argc, char* argv[]) {
    // Manual arg parse: three required positionals plus the optional --no-filter flag.
    std::vector<std::string> positionals;
    bool disable_filter = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-filter") {
            disable_filter = true;
        } else if (arg.size() >= 2 && arg.substr(0, 2) == "--") {
            std::cerr << "Error: unknown flag: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        } else {
            positionals.push_back(arg);
        }
    }

    if (positionals.size() != 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string query_file = positionals[0];
    std::string input_dir = positionals[1];
    std::string output_file = positionals[2];

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

        // Step 5: Parse filter conditions from WHERE clause.
        // When --no-filter is set we deliberately leave `filters` empty so the
        // WHERE-clause selection pushdown (our optimization) is skipped and the
        // full unfiltered multi-way join is computed. Join band/equality
        // predicates are unaffected (they come from join_conditions, not filters).
        std::vector<FilterCondition> filters;
        if (disable_filter) {
            std::cout << "FILTER DISABLED (--no-filter): computing full unfiltered "
                         "multi-way join" << std::endl;
        } else {
            for (const auto& filter_str : parsed_query.filter_conditions) {
                FilterCondition filter;
                if (FilterCondition::parse(filter_str, filter)) {
                    filters.push_back(filter);
                } else {
                    std::cerr << "Warning: Failed to parse filter condition: '"
                              << filter_str << "'" << std::endl;
                }
            }
        }

        // Execute oblivious join with debug output and filters
        Table result = ObliviousJoin::ExecuteWithDebug(join_tree, "oblivious_join", filters);

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