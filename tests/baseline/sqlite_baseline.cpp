#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <dirent.h>
#include <sqlite3.h>
#include "app/data_structures/data_structures.h"
#include "app/file_io/table_io.h"
#include "app/file_io/io_entry.h"  // Use IO_Entry for dynamic data
#include "common/debug_util.h"

/* Create SQLite table from plaintext data */
void create_sqlite_table(sqlite3* db, const std::string& table_name, const Table& table) {
    if (table.size() == 0) {
        throw std::runtime_error("Cannot create table from empty data");
    }
    
    // Get column names from table schema
    std::vector<std::string> schema = table.get_schema();
    if (schema.empty()) {
        throw std::runtime_error("Table has no schema set - cannot create SQLite table");
    }
    
    // Build CREATE TABLE statement
    std::string create_sql = "CREATE TABLE " + table_name + " (";
    for (size_t i = 0; i < schema.size(); i++) {
        if (i > 0) create_sql += ", ";
        create_sql += schema[i] + " INTEGER";
    }
    create_sql += ")";
    
    // Execute CREATE TABLE
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, create_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string error = std::string("SQL error: ") + err_msg;
        sqlite3_free(err_msg);
        throw std::runtime_error(error);
    }
    
    // Insert data
    for (const auto& entry : table) {
        // Convert Entry to IO_Entry for easier attribute access
        IO_Entry io_entry(entry, schema);
        std::string insert_sql = "INSERT INTO " + table_name + " VALUES (";
        for (size_t i = 0; i < io_entry.attributes.size(); i++) {
            if (i > 0) insert_sql += ", ";
            insert_sql += std::to_string(io_entry.attributes[i]);
        }
        insert_sql += ")";
        
        rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::string error = std::string("SQL error during insert: ") + err_msg;
            sqlite3_free(err_msg);
            throw std::runtime_error(error);
        }
    }
    
    // Table created
}

/* Callback for SQLite query results */
struct QueryResult {
    Table* table = nullptr;  // Will be initialized when we know the schema
    std::vector<std::string> column_names;
    bool first_row = true;
};

static int query_callback(void* data, int argc, char** argv, char** col_names) {
    QueryResult* result = (QueryResult*)data;
    
    // On first row, get column names and create table
    if (result->first_row) {
        for (int i = 0; i < argc; i++) {
            result->column_names.push_back(col_names[i]);
        }
        // Now create the table with the schema
        result->table = new Table("result", result->column_names);
        result->first_row = false;
    }
    
    // Create IO_Entry for this row (dynamic size)
    IO_Entry io_entry;
    io_entry.column_names = result->column_names;
    for (int i = 0; i < argc; i++) {
        io_entry.attributes.push_back(argv[i] ? std::stoi(argv[i]) : 0);
    }
    
    // Convert to fixed-size Entry and add to table
    result->table->add_entry(io_entry.to_entry());
    return 0;
}

/* Execute join query and get result */
Table execute_sqlite_join(sqlite3* db, const std::string& join_query) {
    QueryResult result;
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, join_query.c_str(), query_callback, &result, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::string error = std::string("SQL error during join: ") + err_msg;
        sqlite3_free(err_msg);
        throw std::runtime_error(error);
    }
    
    // Query executed
    
    // If no results, create empty table with generic schema
    if (!result.table) {
        return Table("result", {"col1"});
    }
    
    // Set the schema for the result table (already set in callback)
    // result.table->set_schema(result.column_names);  // Already done in callback
    
    Table ret_table = *result.table;
    delete result.table;
    return ret_table;
}

/* Read SQL query from file */
std::string read_sql_query(const std::string& sql_file) {
    std::ifstream file(sql_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open SQL file: " + sql_file);
    }
    
    std::string query;
    std::string line;
    while (std::getline(file, line)) {
        // Skip comment lines
        if (line.find("--") == 0) continue;
        if (!line.empty()) {
            query += line + " ";
        }
    }
    
    file.close();
    return query;
}

/* Print usage information */
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <sql_file> <input_dir> <output_file>" << std::endl;
    std::cout << "  sql_file    : SQL file containing the query" << std::endl;
    std::cout << "  input_dir   : Directory containing plaintext CSV table files" << std::endl;
    std::cout << "  output_file : Output file for plaintext join result" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string sql_file = argv[1];
    std::string input_dir = argv[2];
    std::string output_file = argv[3];
    
    // Starting SQLite baseline
    
    sqlite3* db = nullptr;

    try {
        // Create in-memory SQLite database
        int rc = sqlite3_open(":memory:", &db);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Cannot open SQLite database");
        }
        // Database created

        // Load all CSV files (plaintext for TDX)
        // Loading tables
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

                // Loading file
                Table plaintext_table = TableIO::load_csv(filepath);

                // Create SQLite table
                create_sqlite_table(db, table_name, plaintext_table);

                tables.emplace(table_name, std::move(plaintext_table));
            }
        }
        closedir(dir);

        if (tables.empty()) {
            throw std::runtime_error("No CSV files found in input directory");
        }

        // Read and execute SQL query
        // Reading query
        std::string join_query = read_sql_query(sql_file);
        // Query loaded

        Table join_result = execute_sqlite_join(db, join_query);

        // Save result (plaintext for TDX)
        // Saving result
        TableIO::save_csv(join_result, output_file);
        printf("Result: %zu rows\n", join_result.size());

        // Cleanup
        sqlite3_close(db);

        // Join complete
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (db) sqlite3_close(db);
        return 1;
    }
}