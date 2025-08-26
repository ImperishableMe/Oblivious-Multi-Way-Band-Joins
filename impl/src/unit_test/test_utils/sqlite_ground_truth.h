#ifndef SQLITE_GROUND_TRUTH_H
#define SQLITE_GROUND_TRUTH_H

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include "../../app/types.h"

/**
 * SQLiteGroundTruth - Reference implementation using SQLite
 * 
 * Creates an in-memory SQLite database, loads tables, executes queries,
 * and returns results for comparison with our join implementation.
 * 
 * This provides a trusted reference for validating join correctness.
 */
class SQLiteGroundTruth {
private:
    sqlite3* db;
    bool is_open;
    
    /**
     * Execute a SQL statement that doesn't return results
     */
    void execute_statement(const std::string& sql);
    
    /**
     * Create table schema from a Table object
     */
    std::string create_table_schema(const std::string& table_name, const Table& table);
    
    /**
     * Insert all entries from a Table into SQLite
     */
    void insert_table_data(const std::string& table_name, const Table& table);
    
    /**
     * Convert SQLite query results to Table object
     */
    static int query_callback(void* data, int argc, char** argv, char** col_names);
    
    /**
     * Helper structure for query callback
     */
    struct QueryResult {
        Table result_table;
        std::vector<std::string> column_names;
        bool first_row = true;
    };
    
public:
    SQLiteGroundTruth();
    ~SQLiteGroundTruth();
    
    /**
     * Open/create an in-memory database
     */
    void open_database();
    
    /**
     * Close the database connection
     */
    void close_database();
    
    /**
     * Load a table into the database
     * Creates schema and inserts all data
     * 
     * @param name Table name
     * @param table Table data (should be decrypted)
     */
    void load_table(const std::string& name, const Table& table);
    
    /**
     * Execute a SQL query and return results
     * 
     * @param sql SQL query string
     * @return Result table
     */
    Table execute_query(const std::string& sql);
    
    /**
     * Clear all tables from database
     */
    void clear_database();
    
    /**
     * Check if database is open
     */
    bool is_database_open() const { return is_open; }
};

#endif // SQLITE_GROUND_TRUTH_H