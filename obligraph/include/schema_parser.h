#pragma once

#include <string>
#include <vector>
#include <map>
#include "definitions.h"

namespace obligraph {

/**
 * Represents a parsed table definition from Cypher schema
 */
struct TableDefinition {
    std::string name;
    TableType type;  // NODE or EDGE
    std::vector<std::pair<std::string, ColumnType>> columns;  // (name, type)
    std::vector<std::string> primaryKeys;

    // Edge-specific fields
    std::string srcNodeTable;  // FROM clause
    std::string destNodeTable; // TO clause
};

/**
 * Parse a Cypher schema file containing CREATE NODE TABLE and CREATE REL TABLE statements
 *
 * @param schemaPath Path to the .cypher schema file
 * @return Map of table name to table definition
 * @throws std::runtime_error on parse errors
 */
std::map<std::string, TableDefinition> parseCypherSchema(const std::string& schemaPath);

/**
 * Load a CSV file using a pre-defined schema (no headers in CSV)
 *
 * @param catalog The catalog to add the table to
 * @param tableDef The table definition from schema
 * @param csvPath Path to the CSV file
 * @throws std::runtime_error on file/data errors
 */
void loadTableFromCSV(Catalog& catalog, const TableDefinition& tableDef, const std::string& csvPath);

/**
 * Helper: Convert Cypher type string to ColumnType enum
 * Supports: INT32, INT64, STRING, DOUBLE, BOOLEAN, DATE, TIMESTAMP, BLOB
 */
ColumnType cypherTypeToColumnType(const std::string& typeStr);

} // namespace obligraph
