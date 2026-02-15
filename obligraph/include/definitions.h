#pragma once
#include <variant>
#include <string>
#include <cstdint>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <map>
#include <algorithm>

#include "obl_building_blocks.h"

using namespace std;    


namespace obligraph {
constexpr int STRING_LENGTH_CUT_OFF = 64; // All strings are assumed to be of 64 bytes, anything larger will be truncated
constexpr size_t ROW_DATA_MAX_SIZE = 64; // Maximum size for row data in bytes


using key_t = uint64_t;  // Type for primary keys

// Make a type enum for different column types
enum class ColumnType {
    INT32,
    INT64,
    STRING,
    DOUBLE,
    BOOLEAN,
    DATE,
    TIMESTAMP,
    BLOB,
    UNKNOWN
};

// Helper function to parse column type from string
inline ColumnType parseColumnType(const string& typeStr) {
    if (typeStr == "int32") return ColumnType::INT32;
    if (typeStr == "int64") return ColumnType::INT64;
    if (typeStr == "string") return ColumnType::STRING;
    if (typeStr == "double") return ColumnType::DOUBLE;
    if (typeStr == "boolean") return ColumnType::BOOLEAN;
    if (typeStr == "date") return ColumnType::DATE;
    if (typeStr == "timestamp") return ColumnType::TIMESTAMP;
    if (typeStr == "blob") return ColumnType::BLOB;
    return ColumnType::UNKNOWN;
}

// Helper function to get size of fixed-width types
inline size_t getColumnTypeSize(ColumnType type) {
    switch (type) {
        case ColumnType::INT32: return sizeof(int32_t);
        case ColumnType::INT64: return sizeof(int64_t);
        case ColumnType::DOUBLE: return sizeof(double);
        case ColumnType::BOOLEAN: return sizeof(bool);
        case ColumnType::STRING: return 0; // Variable length
        case ColumnType::DATE: return sizeof(int64_t); // Store as timestamp
        case ColumnType::TIMESTAMP: return sizeof(int64_t);
        case ColumnType::BLOB: return 0; // Variable length
        default: return 0;
    }
}

inline string getColumnTypeString(ColumnType type) {
    switch (type) {
        case ColumnType::INT32: return "int32";
        case ColumnType::INT64: return "int64";
        case ColumnType::STRING: return "string";
        case ColumnType::DOUBLE: return "double";
        case ColumnType::BOOLEAN: return "boolean";
        case ColumnType::DATE: return "date";
        case ColumnType::TIMESTAMP: return "timestamp";
        case ColumnType::BLOB: return "blob";
        default: return "unknown";
    }
}

enum class TableType {
    NODE,
    EDGE,
    INTERMEDIATE
};

struct ColumnMeta {
    string name;  // Name of the column
    ColumnType type;  // Type of the column (e.g., INT, STRING, etc.)
    size_t size;  // Size of the column in bytes
    size_t offset;  // Offset of the column in the row data
};


struct Schema {
    vector<ColumnMeta> columnMetas;  // List of columns in the schema
};

using PairKey = pair<key_t, key_t>;  // Pair of primary keys for edge tables

// Type alias for column values that can be returned
// Note: ColumnTypes are different from ColumnValue's type. 
// ColumnTypes are meant for the user of the DB,
// while ColumnValue's type is meant for the internal representation of the DB.
using ColumnValue = variant<int32_t, int64_t, string, double, bool>;

struct Predicate {
    string column;  // Column name to evaluate
    enum class Cmp { EQ, GT, LT, GTE, LTE } op;  // Comparison operator
    ColumnValue constant;  // Constant value to compare against
};

struct Row {
    char data[ROW_DATA_MAX_SIZE];  // Fixed-size raw data for the row
    size_t size;  // Size of the actual data in bytes
    PairKey key;  // Pair of primary keys for tables, for node table, the second key is 0
    bool isDummy = false;  // Flag to indicate if this is a dummy row

    // Get column value by name
    ColumnValue getColumnValue(const string& columnName, const Schema& schema) const;
};

struct Table {
    string name;  // Name of the table
    string alias;  // Alias for the table, if any
    Schema schema;  // Schema of the table
    vector<ColumnMeta> primaryKeys; // one Pkey for Node Table, two Pkeys (srcId, destId) for Edge Table
    vector<Row> rows;  // List of rows in the table
    TableType type;  // Type of the table (e.g., NODE, EDGE, INTERMEDIATE)
    size_t rowCount = 0;  // Number of rows in the table

    // Edge specific attributes
    vector <string> node_table_names; // {srcNodeName, destNodeName} for edge tables

    // initialize table with another table's metadata
    void init(const Table& other) {
        name = other.name;
        schema = other.schema;
        primaryKeys = other.primaryKeys;
        type = other.type;
        rowCount = 0;
    }

    void addRow(const Row& row) {
        if (row.size > ROW_DATA_MAX_SIZE) {
            throw runtime_error("Row size exceeds maximum allowed size");
        }
        rows.push_back(row);
        rowCount++;
    }

    // Print table information
    void print() const;

    // Project table to include only specified columns
    Table project(const vector<string>& columnNames, ThreadPool& pool) const;

    // filter table rows based on a list of predicates
    void filter(const vector<Predicate>& predicates, ThreadPool& pool);


    // Union operator to combine two tables of same size, but different schemas
    // Note: This assumes both tables have the same number of rows and compatible schemas
    // Expand the schema of the current table to include all columns from the other table
    // Modifies the current table in place.
    // If the column exists in both tables, it will keep the current table's column
    // If the column exists only in the other table, it will add it to the current
    // If columnPrefix is provided, all columns from 'other' will be prefixed (e.g., "Person_name")
    void unionWith(const Table& other, ThreadPool& pool, const string& columnPrefix = "");

    void printSchema() {
        cout << "Printing the schema: " << endl;
        for (const auto& meta : schema.columnMetas) {
            cout << "Column Name: " << meta.name << endl;
            cout << "Column Type: " << getColumnTypeString(meta.type) << endl;
            cout << "Column Size: " << meta.size << endl;
            cout << "Column Offset: " << meta.offset << endl;
        }
    }
};


struct Catalog {
    vector<Table> tables;  // List of tables in the catalog

    Table& getTable(const string& tableName) {
        for (auto& table : tables) {
            if (table.name == tableName) {
                return table;
            }
        }
        throw runtime_error("Table '" + tableName + "' not found in catalog");
    }

    /**
        Imports a node table specified in the CSV file.
        Reads the CSV file, parse the data, and create a Table object, and finally add it to the catalog.

        Implementation details:
        - The CSV filename without '.csv' extension is used as the table name.
        - The CSV file uses '|' as the delimiter.
        - The first line contains the column names.
        - The second line contains the column types.
        - Subsequent lines contain the actual data rows.
        - Each data row must have the same number of fields as the column names.
    **/
    void importNodeFromCSV(const string &filePath);
    
    /**
     * Imports an edge table specified in the CSV file.
     * Implementation details:
     * - The CSV filename is in format <node_table_name>_<edge_table_name>_<destination_node_name> without '.csv' extension is used as the table name. So, we can parse the table name from the file path.
     * - The CSV file uses '|' as the delimiter.
     * - The first line contains the column names. There is no "id" column in edge tables, 
     *      instead the primary keys are build with composite keys (srcId, destId).
     *      To figure out the primary keys, look for the columns that match the node table names.
     *      For example, if the edge table is between "user" and "post", the primary keys will be "userId" and "postId".
     *      However, if both the source and destination nodes are of the same type, the id names will be numbered like "user1Id", "user2Id", etc.
     *      For example, if the edge table is between "person" and "person", the primary keys will be "person1Id" and "person2Id". 
     * - The second line contains the column types just like node tables.
     * - The subsequent lines contain the actual data rows.
     * - Each data row must have the same number of fields as the column names.
     * - The edge table will have two primary keys (srcId, destId) and the data will be stored in the same way as node tables.
     * 
     *  Important: We will store two copies of the edge table:
     *     1. <edge_table_name>_fwd: for forward edges (srcId -> destId), it will sorted by srcId, and ties will be broken by destId.
     *     2. <edge_table_name>_rev: for reverse edges (destId -> srcId), it will sorted by destId, and ties will be broken by srcId.
    **/

    void importEdgeFromCSV(const string &filePath);

    // Print catalog information
    void print() const {
        cout << "=== CATALOG ===" << endl;
        cout << "Total tables: " << tables.size() << endl;
        cout << endl;
        
        for (const auto& table : tables) {
            table.print();
        }
    }

    // Helper function to serialize a row's data based on schema
    void serializeRowData(Row& row, const vector<ColumnMeta>& columnMetas, const vector<string>& values);

private:
    // Helper function to split string by delimiter
    vector<string> splitString(const string& str, char delimiter);
};

// Struct to encapsulate all parameters for one-hop cypher query
struct OneHopQuery {
    string sourceNodeTableName;
    string edgeTableName;
    string destNodeTableName;
    vector<pair<string, vector<Predicate>>> tablePredicates; // (tableName, predicates)
    vector<pair<string, string>> projectionColumns; // (tableName, columnName)
    
    OneHopQuery(const string& srcTableName, const string& edgeTableName, const string& destTableName,
                const vector<pair<string, vector<Predicate>>>& predicates = {},
                const vector<pair<string, string>>& projCols = {})
        : sourceNodeTableName(srcTableName), edgeTableName(edgeTableName), destNodeTableName(destTableName),
            tablePredicates(predicates), projectionColumns(projCols) {}
};

Table oneHop(Catalog& catalog, OneHopQuery& query, ThreadPool& pool);

} // namespace obligraph
