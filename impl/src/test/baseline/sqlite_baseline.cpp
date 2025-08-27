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
#include "../../app/Enclave_u.h"

/* Global enclave ID for decryption/encryption */
sgx_enclave_id_t global_eid = 0;

/* Initialize the enclave */
int initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    ret = sgx_create_enclave("../../enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: 0x" << std::hex << ret << std::endl;
        return -1;
    }
    
    std::cout << "SGX Enclave initialized for encryption/decryption" << std::endl;
    return 0;
}

/* Destroy the enclave */
void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
        std::cout << "SGX Enclave destroyed" << std::endl;
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
    
    std::cout << "  Created SQLite table " << table_name << " with " 
              << table.size() << " rows" << std::endl;
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
    
    std::cout << "Join query executed, result has " << result.table.size() << " rows" << std::endl;
    
    return result.table;
}

/* Parse join specification to create SQL query */
std::string create_join_query(const std::string& spec_file) {
    // For now, return a simple join query
    // TODO: Parse spec file properly
    return "SELECT * FROM customer INNER JOIN orders ON customer.id = orders.customer_id";
}

/* Print usage information */
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <input_dir> <join_spec> <output_file>" << std::endl;
    std::cout << "  input_dir   : Directory containing encrypted CSV table files" << std::endl;
    std::cout << "  join_spec   : Join specification file" << std::endl;
    std::cout << "  output_file : Output file for encrypted join result" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string input_dir = argv[1];
    std::string join_spec = argv[2];
    std::string output_file = argv[3];
    
    std::cout << "\n=== SQLite Baseline Join ===" << std::endl;
    std::cout << "Input directory: " << input_dir << std::endl;
    std::cout << "Join specification: " << join_spec << std::endl;
    std::cout << "Output file: " << output_file << std::endl;
    
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
        std::cout << "\nSQLite database created" << std::endl;
        
        // Load and decrypt all CSV files
        std::cout << "\nLoading and decrypting tables..." << std::endl;
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
                
                std::cout << "  Loading " << filename << "..." << std::endl;
                Table encrypted_table = TableIO::load_csv(filepath);
                
                std::cout << "  Decrypting " << table_name << "..." << std::endl;
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
        
        // Create and execute join query
        std::cout << "\nExecuting join query..." << std::endl;
        std::string join_query = create_join_query(join_spec);
        std::cout << "Query: " << join_query << std::endl;
        
        Table join_result = execute_sqlite_join(db, join_query);
        
        // Encrypt the result
        std::cout << "\nEncrypting result..." << std::endl;
        Table encrypted_result = encrypt_table(join_result);
        
        // Save result
        std::cout << "Saving result to " << output_file << "..." << std::endl;
        TableIO::save_csv(encrypted_result, output_file);
        std::cout << "Result saved (" << encrypted_result.size() << " rows)" << std::endl;
        
        // Cleanup
        sqlite3_close(db);
        destroy_enclave();
        
        std::cout << "\n=== SQLite Join Complete ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (db) sqlite3_close(db);
        destroy_enclave();
        return 1;
    }
}