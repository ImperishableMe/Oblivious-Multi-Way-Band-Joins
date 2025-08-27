#include "sqlite_ground_truth.h"
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <iomanip>

SQLiteGroundTruth::SQLiteGroundTruth() : db(nullptr), is_open(false) {
}

SQLiteGroundTruth::~SQLiteGroundTruth() {
    if (is_open) {
        close_database();
    }
}

void SQLiteGroundTruth::open_database() {
    if (is_open) {
        close_database();
    }
    
    // Open in-memory database
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to open SQLite database: " + 
                               std::string(sqlite3_errmsg(db)));
    }
    
    is_open = true;
}

void SQLiteGroundTruth::close_database() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
    is_open = false;
}

void SQLiteGroundTruth::execute_statement(const std::string& sql) {
    if (!is_open) {
        throw std::runtime_error("Database not open");
    }
    
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_msg);
    
    if (rc != SQLITE_OK) {
        std::string error = error_msg ? error_msg : "Unknown error";
        sqlite3_free(error_msg);
        throw std::runtime_error("SQL execution failed: " + error + "\nSQL: " + sql);
    }
}

std::string SQLiteGroundTruth::create_table_schema(const std::string& table_name, const Table& table) {
    if (table.size() == 0) {
        throw std::runtime_error("Cannot create schema from empty table");
    }
    
    // Get column names from first entry
    Entry first = table[0];
    auto fields = first.get_attributes_map();
    
    std::stringstream sql;
    sql << "CREATE TABLE " << table_name << " (";
    
    bool first_col = true;
    for (const auto& [col_name, value] : fields) {
        if (!first_col) sql << ", ";
        first_col = false;
        
        // Remove table prefix if present (e.g., "supplier.S_NATIONKEY" -> "S_NATIONKEY")
        std::string clean_name = col_name;
        size_t dot_pos = col_name.find('.');
        if (dot_pos != std::string::npos) {
            clean_name = col_name.substr(dot_pos + 1);
        }
        
        // Use INTEGER for all columns (simplification)
        sql << clean_name << " INTEGER";
    }
    
    sql << ")";
    
    return sql.str();
}

void SQLiteGroundTruth::load_table(const std::string& name, const Table& table) {
    if (!is_open) {
        open_database();
    }
    
    // Create table schema
    std::string create_sql = create_table_schema(name, table);
    execute_statement(create_sql);
    
    // Insert all data
    insert_table_data(name, table);
}

void SQLiteGroundTruth::insert_table_data(const std::string& table_name, const Table& table) {
    if (table.size() == 0) return;
    
    // Get column names from first entry
    Entry first = table[0];
    auto fields = first.get_attributes_map();
    
    // Build column list
    std::vector<std::string> col_names;
    for (const auto& [col_name, value] : fields) {
        // Remove table prefix if present
        std::string clean_name = col_name;
        size_t dot_pos = col_name.find('.');
        if (dot_pos != std::string::npos) {
            clean_name = col_name.substr(dot_pos + 1);
        }
        col_names.push_back(clean_name);
    }
    
    // Begin transaction for performance
    execute_statement("BEGIN TRANSACTION");
    
    // Insert each row
    for (size_t i = 0; i < table.size(); i++) {
        Entry entry = table[i];
        auto entry_fields = entry.get_attributes_map();
        
        std::stringstream sql;
        sql << "INSERT INTO " << table_name << " (";
        
        // Column names
        for (size_t j = 0; j < col_names.size(); j++) {
            if (j > 0) sql << ", ";
            sql << col_names[j];
        }
        
        sql << ") VALUES (";
        
        // Values
        bool first_val = true;
        for (const auto& [col_name, value] : entry_fields) {
            if (!first_val) sql << ", ";
            first_val = false;
            sql << static_cast<int>(value);
        }
        
        sql << ")";
        
        execute_statement(sql.str());
    }
    
    // Commit transaction
    execute_statement("COMMIT");
}

int SQLiteGroundTruth::query_callback(void* data, int argc, char** argv, char** col_names) {
    QueryResult* result = static_cast<QueryResult*>(data);
    
    // On first row, store column names
    if (result->first_row) {
        result->column_names.clear();
        for (int i = 0; i < argc; i++) {
            result->column_names.push_back(col_names[i] ? col_names[i] : "");
        }
        result->first_row = false;
    }
    
    // Create entry for this row
    Entry entry;
    for (int i = 0; i < argc; i++) {
        if (argv[i]) {
            int32_t value = std::stoi(argv[i]);
            entry.add_attribute(result->column_names[i], value);
        } else {
            entry.add_attribute(result->column_names[i], 0);  // NULL as 0
        }
    }
    
    entry.set_is_encrypted(false);
    result->result_table.add_entry(entry);
    
    return 0;  // Continue processing rows
}

Table SQLiteGroundTruth::execute_query(const std::string& sql) {
    if (!is_open) {
        throw std::runtime_error("Database not open");
    }
    
    QueryResult result;
    result.result_table = Table("query_result");
    
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), query_callback, &result, &error_msg);
    
    if (rc != SQLITE_OK) {
        std::string error = error_msg ? error_msg : "Unknown error";
        sqlite3_free(error_msg);
        throw std::runtime_error("Query execution failed: " + error);
    }
    
    return result.result_table;
}

void SQLiteGroundTruth::clear_database() {
    if (!is_open) return;
    
    // Get list of tables
    std::vector<std::string> tables;
    
    const char* sql = "SELECT name FROM sqlite_master WHERE type='table'";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* table_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (table_name) {
                tables.push_back(table_name);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Drop all tables
    for (const auto& table : tables) {
        execute_statement("DROP TABLE IF EXISTS " + table);
    }
}