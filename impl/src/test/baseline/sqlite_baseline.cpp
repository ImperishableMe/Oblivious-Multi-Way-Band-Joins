#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <dirent.h>
#include <sqlite3.h>
#include "../../app/data_structures/types.h"
#include "../../app/io/table_io.h"
#include "../../app/crypto/crypto_utils.h"
#include "sgx_urts.h"
#include "../../common/debug_util.h"
#include "../../app/Enclave_u.h"

/* Global enclave ID for decryption/encryption */
sgx_enclave_id_t global_eid = 0;

/* Initialize the enclave */
int initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
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

/* Decrypt a table for SQLite processing */
Table decrypt_table(const Table& encrypted_table) {
    Table decrypted = encrypted_table;
    
    for (size_t i = 0; i < decrypted.size(); i++) {
        Entry& entry = decrypted[i];
        if (entry.is_encrypted) {
            CryptoUtils::decrypt_entry(entry, global_eid);
        }
    }
    
    return decrypted;
}

/* Encrypt a table after SQLite processing */
Table encrypt_table(const Table& plain_table) {
    Table encrypted = plain_table;
    
    for (size_t i = 0; i < encrypted.size(); i++) {
        Entry& entry = encrypted[i];
        if (!entry.is_encrypted) {
            CryptoUtils::encrypt_entry(entry, global_eid);
        }
    }
    
    return encrypted;
}

/* Create SQLite table from decrypted data */
void create_sqlite_table(sqlite3* db, const std::string& table_name, const Table& table) {
    if (table.size() == 0) {
        throw std::runtime_error("Cannot create table from empty data");
    }
    
    // Get column names from first entry
    const Entry& first = table[0];
    
    // Build CREATE TABLE statement
    std::string create_sql = "CREATE TABLE " + table_name + " (";
    for (size_t i = 0; i < first.column_names.size(); i++) {
        if (i > 0) create_sql += ", ";
        create_sql += first.column_names[i] + " INTEGER";
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
        std::string insert_sql = "INSERT INTO " + table_name + " VALUES (";
        for (size_t i = 0; i < entry.attributes.size(); i++) {
            if (i > 0) insert_sql += ", ";
            insert_sql += std::to_string(entry.attributes[i]);
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
    Table table;
    std::vector<std::string> column_names;
    bool first_row = true;
};

static int query_callback(void* data, int argc, char** argv, char** col_names) {
    QueryResult* result = (QueryResult*)data;
    
    // On first row, get column names
    if (result->first_row) {
        for (int i = 0; i < argc; i++) {
            result->column_names.push_back(col_names[i]);
        }
        result->first_row = false;
    }
    
    // Create entry for this row
    Entry entry;
    entry.column_names = result->column_names;
    for (int i = 0; i < argc; i++) {
        entry.attributes.push_back(argv[i] ? std::stoi(argv[i]) : 0);
    }
    
    result->table.add_entry(entry);
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
    
    return result.table;
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
    std::cout << "  input_dir   : Directory containing encrypted CSV table files" << std::endl;
    std::cout << "  output_file : Output file for encrypted join result" << std::endl;
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
        // Initialize the enclave for crypto operations
        if (initialize_enclave() < 0) {
            std::cerr << "Enclave initialization failed!" << std::endl;
            return -1;
        }
        
        // Create in-memory SQLite database
        int rc = sqlite3_open(":memory:", &db);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Cannot open SQLite database");
        }
        // Database created
        
        // Load and decrypt all CSV files
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
                Table encrypted_table = TableIO::load_csv(filepath);
                
                // Decrypting
                Table decrypted_table = decrypt_table(encrypted_table);
                
                // Create SQLite table
                create_sqlite_table(db, table_name, decrypted_table);
                
                tables[table_name] = decrypted_table;
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
        
        // Debug output removed for cleaner output
        
        Table join_result = execute_sqlite_join(db, join_query);
        
        // Encrypt the result
        // Encrypting result
        Table encrypted_result = encrypt_table(join_result);
        
        // Save result (encrypted with nonce)
        // Saving result
        TableIO::save_encrypted_csv(encrypted_result, output_file, global_eid);
        printf("Result: %zu rows\n", encrypted_result.size());
        
        // Cleanup
        sqlite3_close(db);
        destroy_enclave();
        
        // Join complete
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (db) sqlite3_close(db);
        destroy_enclave();
        return 1;
    }
}