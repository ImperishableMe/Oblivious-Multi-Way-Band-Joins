#include "definitions.h"

namespace obligraph {

void Table::print() const {
    cout << "Table: " << name << endl;
    cout << "Type: " << (type == TableType::NODE ? "NODE" : 
                         type == TableType::EDGE ? "EDGE" : "INTERMEDIATE") << endl;
    cout << "Row Count: " << rowCount << endl;
    cout << "Primary Keys: ";
    for (size_t i = 0; i < primaryKeys.size(); i++) {
        cout << primaryKeys[i].name;
        if (i < primaryKeys.size() - 1) cout << ", ";
    }
    cout << endl;
    
    cout << "Schema:" << endl;
    for (const auto& meta : schema.columnMetas) {
        cout << "  - " << meta.name << " (" << getColumnTypeString(meta.type) 
             << ", size: " << meta.size << ", offset: " << meta.offset << ")" << endl;
    }
    cout << "Total rows: " << rows.size() << endl;
    
    // Print rows data
    if (!rows.empty()) {
        cout << "\nData:" << endl;
        
        // Print column headers
        cout << "| ";
        for (size_t i = 0; i < schema.columnMetas.size(); i++) {
            cout << schema.columnMetas[i].name;
            if (i < schema.columnMetas.size() - 1) cout << " | ";
        }
        cout << " |" << endl;
        
        // Print separator line
        cout << "|";
        for (size_t i = 0; i < schema.columnMetas.size(); i++) {
            cout << string(schema.columnMetas[i].name.length() + 2, '-');
            if (i < schema.columnMetas.size() - 1) cout << "|";
        }
        cout << "|" << endl;
        
        // Print first 10 rows or all if less than 10
        size_t maxRowsToShow = min(rows.size(), static_cast<size_t>(10));
        if (maxRowsToShow == 0) {
            cout << "No data available." << endl;
            return;
        }
        cout << "Showing first " << maxRowsToShow << " rows:" << endl;

        for (size_t i = 0; i < maxRowsToShow; i++) {
            const auto& row = rows[i];
            cout << "| ";
            for (size_t i = 0; i < schema.columnMetas.size(); i++) {
                try {
                    ColumnValue value = row.getColumnValue(schema.columnMetas[i].name, schema);
                    
                    // Print value based on its type
                    visit([](const auto& val) {
                        cout << val;
                    }, value);
                    
                    if (i < schema.columnMetas.size() - 1) cout << " | ";
                } catch (const exception& e) {
                    cout << "ERROR";
                    if (i < schema.columnMetas.size() - 1) cout << " | ";
                }
            }
            cout << " |" << endl;
        }
    }
}


void Catalog::importNodeFromCSV(const string &filePath) {
    ifstream file(filePath);
    if (!file.is_open()) {
        throw runtime_error("Cannot open file: " + filePath);
    }

    // Extract table name from file path (remove .csv extension)
    string tableName = filePath;
    size_t lastSlash = tableName.find_last_of("/\\");
    if (lastSlash != string::npos) {
        tableName = tableName.substr(lastSlash + 1);
    }
    size_t lastDot = tableName.find_last_of(".");
    if (lastDot != string::npos) {
        tableName = tableName.substr(0, lastDot);
    }

    string line;
    vector<string> columnNames;
    vector<string> columnTypeStrs;

    // Read column names (first line)
    if (!getline(file, line)) {
        throw runtime_error("Empty file or cannot read column names");
    }
    columnNames = splitString(line, '|');

    // Read column types (second line)
    if (!getline(file, line)) {
        throw runtime_error("Cannot read column types");
    }
    columnTypeStrs = splitString(line, '|');

    if (columnNames.size() != columnTypeStrs.size()) {
        throw runtime_error("Column names and types count mismatch");
    }

    // Create table
    Table table;
    table.name = tableName;
    table.type = TableType::NODE;

    // Build schema with offsets
    size_t currentOffset = 0;
    for (size_t i = 0; i < columnNames.size(); i++) {
        ColumnMeta meta;
        meta.name = columnNames[i];
        meta.type = parseColumnType(columnTypeStrs[i]);
        meta.offset = currentOffset;
        
        if (meta.type == ColumnType::STRING) {
            meta.size = STRING_LENGTH_CUT_OFF; // Fixed size for strings
        } else {
            meta.size = getColumnTypeSize(meta.type);
        }
        
        currentOffset += meta.size;
        table.schema.columnMetas.push_back(meta);
    }

    // Set primary key (look for 'id' column, otherwise use first column)
    size_t primaryKeyIndex = 0;
    for (size_t i = 0; i < columnNames.size(); i++) {
        if (columnNames[i] == "id") {
            primaryKeyIndex = i;
            break;
        }
    }
    if (!columnNames.empty()) {
        table.primaryKeys.push_back(table.schema.columnMetas[primaryKeyIndex]);
    }

    // Read and store data rows
    while (getline(file, line)) {
        if (line.empty()) continue;
        
        vector<string> values = splitString(line, '|');
        if (values.size() != columnNames.size()) {
            cout << "Data row has incorrect number of fields: " << line << endl;
            throw runtime_error("Data row has incorrect number of fields");
        }

        // Create row
        Row row;
        row.size = currentOffset; // Total size of all columns
        // Set primary key (for node tables, second key is 0)
        if (primaryKeyIndex < values.size()) {
            key_t primaryKey = static_cast<key_t>(stoll(values[primaryKeyIndex])); // Use the identified primary key column
            row.key = make_pair(primaryKey, 0);
        }

        // Serialize row data
        serializeRowData(row, table.schema.columnMetas, values);
        
        table.rows.push_back(std::move(row));
        table.rowCount++;
    }

    // Add table to catalog
    tables.push_back(table);
    file.close();
}

vector<string> Catalog::splitString(const string& str, char delimiter) {
    vector<string> tokens;
    stringstream ss(str);
    string token;
    while (getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

void Catalog::serializeRowData(Row& row, const vector<ColumnMeta>& columnMetas, const vector<string>& values) {
    // Validate that the row size doesn't exceed maximum
    if (row.size > ROW_DATA_MAX_SIZE) {
        throw runtime_error("Row data size (" + to_string(row.size) + " bytes) exceeds maximum allowed size (" + to_string(ROW_DATA_MAX_SIZE) + " bytes)");
    }
    
    for (size_t i = 0; i < values.size(); i++) {
        const ColumnMeta& meta = columnMetas[i];
        const string& value = values[i];
        char* dataPtr = row.data + meta.offset;

        switch (meta.type) {
            case ColumnType::INT32: {
                int32_t intVal = stoi(value);
                memcpy(dataPtr, &intVal, sizeof(int32_t));
                break;
            }
            case ColumnType::INT64: {
                int64_t longVal = stoll(value);
                memcpy(dataPtr, &longVal, sizeof(int64_t));
                break;
            }
            case ColumnType::DOUBLE: {
                double doubleVal = stod(value);
                memcpy(dataPtr, &doubleVal, sizeof(double));
                break;
            }
            case ColumnType::BOOLEAN: {
                bool boolVal = (value == "true" || value == "1");
                memcpy(dataPtr, &boolVal, sizeof(bool));
                break;
            }
            case ColumnType::STRING: {
                // Store fixed-length string, truncate if necessary
                string truncatedValue = value;
                if (truncatedValue.length() > STRING_LENGTH_CUT_OFF) {
                    truncatedValue = truncatedValue.substr(0, STRING_LENGTH_CUT_OFF);
                }
                // Pad with null bytes if shorter than STRING_LENGTH_CUT_OFF
                truncatedValue.resize(STRING_LENGTH_CUT_OFF, '\0');
                memcpy(dataPtr, truncatedValue.c_str(), STRING_LENGTH_CUT_OFF);
                break;
            }
            case ColumnType::TIMESTAMP: {
                // For simplicity, store timestamp string as int64 hash
                // In real implementation, you'd parse the ISO timestamp
                int64_t timestamp = hash<string>{}(value);
                memcpy(dataPtr, &timestamp, sizeof(int64_t));
                break;
            }
            case ColumnType::DATE: {
                // For simplicity, store date string as int64 hash
                // In real implementation, you'd parse the date format
                int64_t date = hash<string>{}(value);
                memcpy(dataPtr, &date, sizeof(int64_t));
                break;
            }
            case ColumnType::BLOB: {
                // Store BLOB as fixed-length binary data, similar to STRING
                // For now, treat it as a string (base64 encoded or hex string from CSV)
                string truncatedValue = value;
                size_t blobSize = meta.size;  // Use the size from schema
                if (truncatedValue.length() > blobSize) {
                    truncatedValue = truncatedValue.substr(0, blobSize);
                }
                truncatedValue.resize(blobSize, '\0');
                memcpy(dataPtr, truncatedValue.c_str(), blobSize);
                break;
            }
            default:
                throw runtime_error("Unsupported column type for serialization");
        }
    }
}

// TODO: Add another getColumnValue with the offset value
ColumnValue Row::getColumnValue(const string& columnName, const Schema& schema) const {
    // Find the column in schema
    const ColumnMeta* columnMeta = nullptr;
    for (const auto& meta : schema.columnMetas) {
        if (meta.name == columnName) {
            columnMeta = &meta;
            break;
        }
    }
    
    if (!columnMeta) {
        throw runtime_error("Column '" + columnName + "' not found in schema");
    }
    
    // Extract value based on type
    const char* dataPtr = data + columnMeta->offset;
    
    switch (columnMeta->type) {
        case ColumnType::INT32:
            return *reinterpret_cast<const int32_t*>(dataPtr);
        
        case ColumnType::INT64:
        case ColumnType::DATE:
        case ColumnType::TIMESTAMP:
            return *reinterpret_cast<const int64_t*>(dataPtr);
        
        case ColumnType::DOUBLE:
            return *reinterpret_cast<const double*>(dataPtr);
        
        case ColumnType::BOOLEAN:
            return *reinterpret_cast<const bool*>(dataPtr);
        
        case ColumnType::STRING: {
            // For strings, read until null terminator or STRING_LENGTH_CUT_OFF
            size_t maxLen = min(columnMeta->size, static_cast<size_t>(STRING_LENGTH_CUT_OFF));
            return string(dataPtr, strnlen(dataPtr, maxLen));
        }
        
        default:
            throw runtime_error("Unsupported column type for value extraction");
    }
}

void Catalog::importEdgeFromCSV(const string &filePath) {
    ifstream file(filePath);
    if (!file.is_open()) {
        throw runtime_error("Cannot open file: " + filePath);
    }

    // Extract filename and parse node table names and edge table name
    string fileName = filePath;
    size_t lastSlash = fileName.find_last_of("/\\");
    if (lastSlash != string::npos) {
        fileName = fileName.substr(lastSlash + 1);
    }
    size_t lastDot = fileName.find_last_of(".");
    if (lastDot != string::npos) {
        fileName = fileName.substr(0, lastDot);
    }

    // Parse filename format: <node_table_name>_<edge_table_name>_<destination_node_name>
    vector<string> filenameParts = splitString(fileName, '_');
    if (filenameParts.size() < 3) {
        throw runtime_error("Invalid edge CSV filename format. Expected: <src_node>_<edge>_<dest_node>.csv");
    }
    
    string srcNodeName = filenameParts[0];
    string edgeTableName = filenameParts[1];
    string destNodeName = filenameParts[2];

    string line;
    vector<string> columnNames;
    vector<string> columnTypeStrs;

    // Read column names (first line)
    if (!getline(file, line)) {
        throw runtime_error("Empty file or cannot read column names");
    }
    columnNames = splitString(line, '|');

    // Read column types (second line)
    if (!getline(file, line)) {
        throw runtime_error("Cannot read column types");
    }
    columnTypeStrs = splitString(line, '|');

    if (columnNames.size() != columnTypeStrs.size()) {
        throw runtime_error("Column names and types count mismatch");
    }

    // Helper function to convert string to lowercase for case-insensitive comparison
    auto toLower = [](const string& str) {
        string lower = str;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower;
    };

    // Find primary key columns (case-insensitive search)
    size_t srcIdIndex = SIZE_MAX, destIdIndex = SIZE_MAX;
    string srcIdPattern1 = toLower(srcNodeName + "Id");
    string srcIdPattern2 = toLower(srcNodeName + "1Id");
    string destIdPattern1 = toLower(destNodeName + "Id");
    string destIdPattern2 = toLower(destNodeName + "2Id");

    for (size_t i = 0; i < columnNames.size(); i++) {
        string lowerColName = toLower(columnNames[i]);
        
        // Check for source ID patterns
        if (srcIdIndex == SIZE_MAX && (lowerColName == srcIdPattern1 || lowerColName == srcIdPattern2)) {
            srcIdIndex = i;
        }
        // Check for destination ID patterns
        if (destIdIndex == SIZE_MAX && (lowerColName == destIdPattern1 || lowerColName == destIdPattern2)) {
            destIdIndex = i;
        }
    }

    if (srcIdIndex == SIZE_MAX || destIdIndex == SIZE_MAX) {
        throw runtime_error("Could not find primary key columns for edge table. Expected patterns like: " +
                          srcNodeName + "Id/" + srcNodeName + "1Id and " + destNodeName + "Id/" + destNodeName + "2Id");
    }

    // Build schema with offsets (common for both forward and reverse tables)
    size_t currentOffset = 0;
    vector<ColumnMeta> columnMetas;
    for (size_t i = 0; i < columnNames.size(); i++) {
        ColumnMeta meta;
        meta.name = columnNames[i];
        meta.type = parseColumnType(columnTypeStrs[i]);
        meta.offset = currentOffset;
        
        if (meta.type == ColumnType::STRING) {
            meta.size = STRING_LENGTH_CUT_OFF;
        } else {
            meta.size = getColumnTypeSize(meta.type);
        }
        
        currentOffset += meta.size;
        columnMetas.push_back(meta);
    }

    // Read and store data rows
    vector<Row> dataRows;
    while (getline(file, line)) {
        if (line.empty()) continue;
        
        vector<string> values = splitString(line, '|');
        if (values.size() != columnNames.size()) {
            throw runtime_error("Data row has incorrect number of fields");
        }

        // Create row
        Row row;
        row.size = currentOffset;
        
        // Set composite primary key (srcId, destId)
        key_t srcId = static_cast<key_t>(stoll(values[srcIdIndex]));
        key_t destId = static_cast<key_t>(stoll(values[destIdIndex]));
        row.key = make_pair(srcId, destId);

        // Serialize row data
        serializeRowData(row, columnMetas, values);
        
        dataRows.push_back(std::move(row));
    }
    file.close();

    // Create forward table (sorted by srcId, then destId)
    Table fwdTable;
    fwdTable.name = edgeTableName + "_fwd";
    fwdTable.type = TableType::EDGE;
    fwdTable.schema.columnMetas = columnMetas;
    fwdTable.primaryKeys.push_back(columnMetas[srcIdIndex]);  // srcId
    fwdTable.primaryKeys.push_back(columnMetas[destIdIndex]); // destId
    fwdTable.node_table_names = {srcNodeName, destNodeName};
    fwdTable.rows = dataRows; // Copy data
    fwdTable.rowCount = dataRows.size();

    // Sort forward table by srcId, then destId
    sort(fwdTable.rows.begin(), fwdTable.rows.end(), 
         [](const Row& a, const Row& b) {
            if (a.key.first == b.key.first) {
                return a.key.second < b.key.second;
            }
            return a.key.first < b.key.first;
         });

    // Create reverse table (sorted by destId, then srcId)
    Table revTable;
    revTable.name = edgeTableName + "_rev";
    revTable.type = TableType::EDGE;
    revTable.schema.columnMetas = columnMetas;
    revTable.primaryKeys.push_back(columnMetas[destIdIndex]); // destId
    revTable.primaryKeys.push_back(columnMetas[srcIdIndex]);  // srcId
    revTable.node_table_names = {destNodeName, srcNodeName};

    // Create rows with swapped keys for reverse table
    vector<Row> revDataRows;
    for (const Row& originalRow : dataRows) {
        Row revRow = originalRow; // Copy all data
        // Swap the key components: (srcId, destId) -> (destId, srcId)
        revRow.key = make_pair(originalRow.key.second, originalRow.key.first);
        revDataRows.push_back(std::move(revRow));
    }
    
    revTable.rows = std::move(revDataRows);
    revTable.rowCount = revTable.rows.size();

    // Sort reverse table by destId, then srcId
    sort(revTable.rows.begin(), revTable.rows.end(), 
         [](const Row& a, const Row& b) {
             if (a.key.first == b.key.first) {
                 return a.key.second < b.key.second;
             }
             return a.key.first < b.key.first;
         });

    // Add both tables to catalog
    tables.push_back(std::move(fwdTable));
    tables.push_back(std::move(revTable));
}

} // namespace obligraph
