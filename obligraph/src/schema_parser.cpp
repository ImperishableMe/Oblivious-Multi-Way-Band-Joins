#include "schema_parser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>

namespace obligraph {

// Helper: Trim whitespace from both ends
static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Helper: Convert Cypher type string to ColumnType
ColumnType cypherTypeToColumnType(const std::string& typeStr) {
    std::string lower = typeStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "int32") return ColumnType::INT32;
    if (lower == "int64") return ColumnType::INT64;
    if (lower == "string") return ColumnType::STRING;
    if (lower == "double") return ColumnType::DOUBLE;
    if (lower == "boolean") return ColumnType::BOOLEAN;
    if (lower == "date") return ColumnType::DATE;
    if (lower == "timestamp") return ColumnType::TIMESTAMP;
    if (lower == "blob") return ColumnType::BLOB;

    throw std::runtime_error("Unknown column type: " + typeStr);
}

// Parse a single CREATE NODE TABLE statement
static TableDefinition parseNodeTable(const std::string& statement) {
    TableDefinition def;
    def.type = TableType::NODE;

    // Regex to match: CREATE NODE TABLE TableName(col1 TYPE1, col2 TYPE2, ..., PRIMARY KEY (pk1, pk2))
    std::regex pattern(R"(CREATE\s+NODE\s+TABLE\s+(\w+)\s*\((.*)\))");
    std::smatch match;

    if (!std::regex_search(statement, match, pattern)) {
        throw std::runtime_error("Invalid NODE TABLE syntax: " + statement);
    }

    def.name = match[1].str();
    std::string columnsPart = match[2].str();

    // Find PRIMARY KEY clause
    std::regex pkPattern(R"(PRIMARY\s+KEY\s*\(([^)]+)\))");
    std::smatch pkMatch;

    if (!std::regex_search(columnsPart, pkMatch, pkPattern)) {
        throw std::runtime_error("PRIMARY KEY clause required for NODE TABLE: " + def.name);
    }

    // Extract primary keys
    std::string pkList = pkMatch[1].str();
    std::stringstream pkStream(pkList);
    std::string pk;
    while (std::getline(pkStream, pk, ',')) {
        def.primaryKeys.push_back(trim(pk));
    }

    // Remove PRIMARY KEY clause from columns part
    columnsPart = std::regex_replace(columnsPart, pkPattern, "");

    // Parse columns (col1 TYPE1, col2 TYPE2, ...)
    std::stringstream ss(columnsPart);
    std::string columnDef;

    while (std::getline(ss, columnDef, ',')) {
        columnDef = trim(columnDef);
        if (columnDef.empty()) continue;

        // Split column name and type
        std::istringstream colStream(columnDef);
        std::string colName, colType;
        colStream >> colName >> colType;

        if (colName.empty() || colType.empty()) {
            throw std::runtime_error("Invalid column definition: " + columnDef);
        }

        def.columns.push_back({colName, cypherTypeToColumnType(colType)});
    }

    // Validate primary keys exist in columns
    for (const auto& pk : def.primaryKeys) {
        bool found = false;
        for (const auto& col : def.columns) {
            if (col.first == pk) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("Primary key column '" + pk + "' not found in table " + def.name);
        }
    }

    return def;
}

// Parse a single CREATE REL TABLE statement
static TableDefinition parseRelTable(const std::string& statement) {
    TableDefinition def;
    def.type = TableType::EDGE;

    // Regex to match: CREATE REL TABLE TableName(FROM SrcNode TO DestNode, col1 TYPE1, ...)
    std::regex pattern(R"(CREATE\s+REL\s+TABLE\s+(\w+)\s*\((.*)\))");
    std::smatch match;

    if (!std::regex_search(statement, match, pattern)) {
        throw std::runtime_error("Invalid REL TABLE syntax: " + statement);
    }

    def.name = match[1].str();
    std::string content = match[2].str();

    // Extract FROM ... TO ... clause
    std::regex fromToPattern(R"(FROM\s+(\w+)\s+TO\s+(\w+))");
    std::smatch fromToMatch;

    if (!std::regex_search(content, fromToMatch, fromToPattern)) {
        throw std::runtime_error("FROM ... TO clause required for REL TABLE: " + def.name);
    }

    def.srcNodeTable = fromToMatch[1].str();
    def.destNodeTable = fromToMatch[2].str();

    // Remove FROM ... TO clause from content
    content = std::regex_replace(content, fromToPattern, "");

    // Parse additional columns (optional)
    std::stringstream ss(content);
    std::string columnDef;

    while (std::getline(ss, columnDef, ',')) {
        columnDef = trim(columnDef);
        if (columnDef.empty()) continue;

        // Split column name and type
        std::istringstream colStream(columnDef);
        std::string colName, colType;
        colStream >> colName >> colType;

        if (colName.empty() || colType.empty()) {
            continue;  // No additional columns, just FROM...TO
        }

        def.columns.push_back({colName, cypherTypeToColumnType(colType)});
    }

    // Edge tables implicitly have (srcId, destId) as the first two columns
    // Insert them at the beginning of the columns list
    std::vector<std::pair<std::string, ColumnType>> edgeColumns;

    // Add src and dest ID columns (both INT64)
    // For self-referential relationships, use numbered IDs (e.g., User1Id, User2Id)
    std::string srcIdName, destIdName;
    if (def.srcNodeTable == def.destNodeTable) {
        srcIdName = def.srcNodeTable + "1Id";
        destIdName = def.destNodeTable + "2Id";
    } else {
        srcIdName = def.srcNodeTable + "Id";
        destIdName = def.destNodeTable + "Id";
    }

    edgeColumns.push_back({srcIdName, ColumnType::INT64});
    edgeColumns.push_back({destIdName, ColumnType::INT64});

    // Add any additional relationship properties
    for (const auto& col : def.columns) {
        edgeColumns.push_back(col);
    }

    def.columns = edgeColumns;

    // Primary keys for edges are (srcId, destId)
    def.primaryKeys = {srcIdName, destIdName};

    return def;
}

std::map<std::string, TableDefinition> parseCypherSchema(const std::string& schemaPath) {
    std::ifstream file(schemaPath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open schema file: " + schemaPath);
    }

    std::map<std::string, TableDefinition> tables;
    std::string line;
    std::string statement;

    while (std::getline(file, line)) {
        // Remove comments (-- ...)
        size_t commentPos = line.find("--");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        line = trim(line);
        if (line.empty()) continue;

        statement += " " + line;

        // Check if statement is complete (ends with ))
        if (line.back() == ')') {
            statement = trim(statement);

            // Parse the statement
            if (statement.find("CREATE NODE TABLE") != std::string::npos) {
                TableDefinition def = parseNodeTable(statement);
                if (tables.count(def.name)) {
                    throw std::runtime_error("Duplicate table definition: " + def.name);
                }
                tables[def.name] = def;
            }
            else if (statement.find("CREATE REL TABLE") != std::string::npos) {
                TableDefinition def = parseRelTable(statement);
                if (tables.count(def.name)) {
                    throw std::runtime_error("Duplicate table definition: " + def.name);
                }
                tables[def.name] = def;
            }

            statement.clear();
        }
    }

    return tables;
}

void loadTableFromCSV(Catalog& catalog, const TableDefinition& tableDef, const std::string& csvPath) {
    std::ifstream file(csvPath);
    if (!file.is_open()) {
        throw std::runtime_error("CSV file not found for table: " + tableDef.name);
    }

    // Build schema
    std::vector<ColumnMeta> columnMetas;
    size_t offset = 0;
    for (const auto& [colName, colType] : tableDef.columns) {
        ColumnMeta meta;
        meta.name = colName;
        meta.type = colType;

        // Handle variable-length types with fixed allocation
        if (colType == ColumnType::STRING) {
            meta.size = STRING_LENGTH_CUT_OFF;
        } else if (colType == ColumnType::BLOB) {
            // For BLOB, use same fixed size as STRING
            // In a real system, you might want a different size or make it configurable
            meta.size = STRING_LENGTH_CUT_OFF;
        } else {
            meta.size = getColumnTypeSize(colType);
        }

        meta.offset = offset;

        columnMetas.push_back(meta);
        offset += meta.size;
    }

    // Load CSV data (no headers, just data rows)
    std::vector<Row> rows;
    std::string line;
    size_t rowNum = 0;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        rowNum++;

        // Parse CSV row
        std::vector<std::string> values;
        std::stringstream ss(line);
        std::string value;

        while (std::getline(ss, value, ',')) {
            values.push_back(trim(value));
        }

        // Validate column count
        if (values.size() != tableDef.columns.size()) {
            throw std::runtime_error(
                "Row " + std::to_string(rowNum) + ": Expected " +
                std::to_string(tableDef.columns.size()) + " columns, found " +
                std::to_string(values.size())
            );
        }

        // Create row and serialize data
        Row row;
        memset(row.data, 0, ROW_DATA_MAX_SIZE);

        // Store key values (for edges: srcId, destId)
        if (tableDef.type == TableType::EDGE && values.size() >= 2) {
            row.key.first = std::stoll(values[0]);   // srcId
            row.key.second = std::stoll(values[1]);  // destId
        } else if (tableDef.type == TableType::NODE && !values.empty()) {
            row.key.first = std::stoll(values[0]);   // primary key
            row.key.second = 0;
        }

        // Serialize data into row
        catalog.serializeRowData(row, columnMetas, values);

        rows.push_back(row);
    }

    file.close();

    // For NODE tables, create single table
    if (tableDef.type == TableType::NODE) {
        Table table;
        table.name = tableDef.name;
        table.type = TableType::NODE;
        table.schema.columnMetas = columnMetas;

        // Set primary keys
        for (const auto& pkName : tableDef.primaryKeys) {
            for (const auto& meta : columnMetas) {
                if (meta.name == pkName) {
                    table.primaryKeys.push_back(meta);
                    break;
                }
            }
        }

        table.rows = rows;
        table.rowCount = rows.size();

        catalog.tables.push_back(table);
    }
    // For EDGE tables, create _fwd and _rev tables
    else if (tableDef.type == TableType::EDGE) {
        // Create forward table (sorted by srcId, destId)
        Table fwdTable;
        fwdTable.name = tableDef.name + "_fwd";
        fwdTable.type = TableType::EDGE;
        fwdTable.schema.columnMetas = columnMetas;
        fwdTable.node_table_names = {tableDef.srcNodeTable, tableDef.destNodeTable};

        // Primary keys for forward: srcId (first), destId (second)
        if (columnMetas.size() >= 2) {
            fwdTable.primaryKeys.push_back(columnMetas[0]);  // srcId
            fwdTable.primaryKeys.push_back(columnMetas[1]);  // destId
        }

        fwdTable.rows = rows;
        fwdTable.rowCount = rows.size();

        // Sort forward table by (srcId, destId)
        std::sort(fwdTable.rows.begin(), fwdTable.rows.end(),
                  [](const Row& a, const Row& b) {
                      if (a.key.first != b.key.first)
                          return a.key.first < b.key.first;
                      return a.key.second < b.key.second;
                  });

        // Create reverse table (sorted by destId, srcId)
        Table revTable;
        revTable.name = tableDef.name + "_rev";
        revTable.type = TableType::EDGE;
        revTable.schema.columnMetas = columnMetas;
        revTable.node_table_names = {tableDef.srcNodeTable, tableDef.destNodeTable};

        // Primary keys for reverse: destId (first), srcId (second)
        if (columnMetas.size() >= 2) {
            revTable.primaryKeys.push_back(columnMetas[1]);  // destId
            revTable.primaryKeys.push_back(columnMetas[0]);  // srcId
        }

        revTable.rows = rows;
        revTable.rowCount = rows.size();

        // Swap keys in reverse table so destId is in key.first position
        // This allows build_and_probe to match on destination IDs
        for (auto& row : revTable.rows) {
            std::swap(row.key.first, row.key.second);
        }

        // Sort reverse table by (destId, srcId) - now key.first is destId, key.second is srcId
        std::sort(revTable.rows.begin(), revTable.rows.end(),
                  [](const Row& a, const Row& b) {
                      if (a.key.first != b.key.first)
                          return a.key.first < b.key.first;
                      return a.key.second < b.key.second;
                  });

        catalog.tables.push_back(fwdTable);
        catalog.tables.push_back(revTable);
    }
}

} // namespace obligraph
