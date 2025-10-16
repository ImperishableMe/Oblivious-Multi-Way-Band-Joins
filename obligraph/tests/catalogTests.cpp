#include <gtest/gtest.h>
#include "../include/definitions.h"
#include <fstream>
#include <filesystem>
#include <variant>

using namespace obligraph;

class CatalogTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test CSV file
        testCsvPath = "test_comment.csv";
        std::ofstream file(testCsvPath);
        file << "id|content|hasCreator\n";
        file << "int64|string|int64\n";
        file << "555|This is a reply to the first post.|1099\n";
        file << "666|John replies to Mary's post.|933\n";
        file << "777|A quick comment on the third post.|1099\n";
        file.close();
    }

    void TearDown() override {
        // Clean up test file
        std::filesystem::remove(testCsvPath);
    }

    std::string testCsvPath;
};

TEST_F(CatalogTest, ImportNodeFromCSV_BasicFunctionality) {
    Catalog catalog;
    
    // Import the test CSV
    ASSERT_NO_THROW(catalog.importNodeFromCSV(testCsvPath));
    
    // Check that one table was added
    EXPECT_EQ(catalog.tables.size(), 1);
    
    // Check table properties
    const Table& table = catalog.tables[0];
    EXPECT_EQ(table.name, "test_comment");
    EXPECT_EQ(table.type, TableType::NODE);
    EXPECT_EQ(table.rowCount, 3);
    
    // Check schema
    const Schema& schema = table.schema;
    EXPECT_EQ(schema.columnMetas.size(), 3);
    
    // Check column metadata
    EXPECT_EQ(schema.columnMetas[0].name, "id");
    EXPECT_EQ(schema.columnMetas[0].type, ColumnType::INT64);
    
    EXPECT_EQ(schema.columnMetas[1].name, "content");
    EXPECT_EQ(schema.columnMetas[1].type, ColumnType::STRING);
    
    EXPECT_EQ(schema.columnMetas[2].name, "hasCreator");
    EXPECT_EQ(schema.columnMetas[2].type, ColumnType::INT64);
    
    // Check primary key (should be first column)
    EXPECT_EQ(table.primaryKeys.size(), 1);
    EXPECT_EQ(table.primaryKeys[0].name, "id");
    
    // Check that rows were stored
    EXPECT_GT(table.rows.size(), 0);
}

TEST_F(CatalogTest, ImportNodeFromCSV_FileNotFound) {
    Catalog catalog;
    
    // Try to import non-existent file
    EXPECT_THROW(catalog.importNodeFromCSV("non_existent_file.csv"), std::runtime_error);
}

TEST_F(CatalogTest, ImportNodeFromCSV_MalformedFile) {
    // Create a malformed CSV file
    std::string malformedPath = "malformed.csv";
    std::ofstream file(malformedPath);
    file << "id|name\n";
    file << "int64|string|extra_type\n"; // Mismatch in column count
    file.close();
    
    Catalog catalog;
    EXPECT_THROW(catalog.importNodeFromCSV(malformedPath), std::runtime_error);
    
    // Clean up
    std::filesystem::remove(malformedPath);
}

TEST_F(CatalogTest, ImportNodeFromCSV_DataValidation) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& table = catalog.tables[0];
    
    // Verify that rows were created (check row count and data)
    // Each row should contain:
    // - int64 (8 bytes) for id
    // - string (2 bytes fixed) for content
    // - int64 (8 bytes) for hasCreator
    
    // Should have 3 rows, each with fixed size
    size_t expectedRowSize = 8 + 2 + 8; // 18 bytes per row
    EXPECT_EQ(table.rows.size(), 3);
    if (!table.rows.empty()) {
        EXPECT_EQ(table.rows[0].size, expectedRowSize);
    }
}

TEST_F(CatalogTest, ParseColumnType_AllTypes) {
    EXPECT_EQ(parseColumnType("int32"), ColumnType::INT32);
    EXPECT_EQ(parseColumnType("int64"), ColumnType::INT64);
    EXPECT_EQ(parseColumnType("string"), ColumnType::STRING);
    EXPECT_EQ(parseColumnType("double"), ColumnType::DOUBLE);
    EXPECT_EQ(parseColumnType("boolean"), ColumnType::BOOLEAN);
    EXPECT_EQ(parseColumnType("date"), ColumnType::DATE);
    EXPECT_EQ(parseColumnType("timestamp"), ColumnType::TIMESTAMP);
    EXPECT_EQ(parseColumnType("blob"), ColumnType::BLOB);
    EXPECT_EQ(parseColumnType("unknown_type"), ColumnType::UNKNOWN);
}

TEST_F(CatalogTest, GetColumnTypeSize_FixedTypes) {
    EXPECT_EQ(getColumnTypeSize(ColumnType::INT32), sizeof(int32_t));
    EXPECT_EQ(getColumnTypeSize(ColumnType::INT64), sizeof(int64_t));
    EXPECT_EQ(getColumnTypeSize(ColumnType::DOUBLE), sizeof(double));
    EXPECT_EQ(getColumnTypeSize(ColumnType::BOOLEAN), sizeof(bool));
    EXPECT_EQ(getColumnTypeSize(ColumnType::STRING), 0); // Variable length
    EXPECT_EQ(getColumnTypeSize(ColumnType::BLOB), 0); // Variable length
}

TEST_F(CatalogTest, GetColumnValue_BasicFunctionality) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& table = catalog.tables[0];
    
    // Check values of the first row
    const Row& row = table.rows[0];
    
    auto idValue = row.getColumnValue("id", table.schema);
    EXPECT_TRUE(std::holds_alternative<int64_t>(idValue));
    EXPECT_EQ(std::get<int64_t>(idValue), 555);
    
    auto contentValue = row.getColumnValue("content", table.schema);
    EXPECT_TRUE(std::holds_alternative<std::string>(contentValue));
    EXPECT_EQ(std::get<std::string>(contentValue), "Th"); // Truncated to 2 chars
    
    auto creatorValue = row.getColumnValue("hasCreator", table.schema);
    EXPECT_TRUE(std::holds_alternative<int64_t>(creatorValue));
    EXPECT_EQ(std::get<int64_t>(creatorValue), 1099);
}

TEST_F(CatalogTest, GetColumnValue_AllTypes) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& table = catalog.tables[0];
    const Row& firstRow = table.rows[0];
    const Row& secondRow = table.rows[1];
    const Row& thirdRow = table.rows[2];
    
    // Test getting INT64 column (id)
    auto idValue = firstRow.getColumnValue("id", table.schema);
    EXPECT_TRUE(std::holds_alternative<int64_t>(idValue));
    EXPECT_EQ(std::get<int64_t>(idValue), 555);
    
    // Test getting STRING column (content) - should be truncated to 2 chars
    auto contentValue = firstRow.getColumnValue("content", table.schema);
    EXPECT_TRUE(std::holds_alternative<std::string>(contentValue));
    std::string content = std::get<std::string>(contentValue);
    EXPECT_EQ(content, "Th"); // Truncated to STRING_LENGTH_CUT_OFF (2 chars)
    
    // Test getting INT64 column (hasCreator) from second row
    auto creatorValue = secondRow.getColumnValue("hasCreator", table.schema);
    EXPECT_TRUE(std::holds_alternative<int64_t>(creatorValue));
    EXPECT_EQ(std::get<int64_t>(creatorValue), 933);
}

TEST_F(CatalogTest, GetColumnValue_NonExistentColumn) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& table = catalog.tables[0];
    const Row& firstRow = table.rows[0];
    
    // Test error case - non-existent column
    EXPECT_THROW(firstRow.getColumnValue("nonexistent", table.schema), std::runtime_error);
}

TEST_F(CatalogTest, ProjectTable_BasicFunctionality) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& originalTable = catalog.tables[0];
    
    // Create ThreadPool with size 1
    ThreadPool pool(1);
    
    // Project to subset of columns (excluding primary key to test auto-inclusion)
    vector<string> projectedColumns = {"content", "hasCreator"};
    Table projectedTable = originalTable.project(projectedColumns, pool);
    
    // Verify projected table properties
    EXPECT_EQ(projectedTable.name, originalTable.name + "_projected");
    EXPECT_EQ(projectedTable.type, originalTable.type);
    EXPECT_EQ(projectedTable.rowCount, originalTable.rowCount);
    EXPECT_EQ(projectedTable.rows.size(), originalTable.rows.size());
    
    EXPECT_EQ(projectedTable.schema.columnMetas.size(), 2);
    EXPECT_EQ(projectedTable.schema.columnMetas[0].name, "content");
    EXPECT_EQ(projectedTable.schema.columnMetas[1].name, "hasCreator");
    
    // Verify column types are preserved
    EXPECT_EQ(projectedTable.schema.columnMetas[0].type, ColumnType::STRING);
    EXPECT_EQ(projectedTable.schema.columnMetas[1].type, ColumnType::INT64);
    
    // Verify column offsets are correctly updated for projected schema
    EXPECT_EQ(projectedTable.schema.columnMetas[0].offset, 0);  // content starts at 0
    EXPECT_EQ(projectedTable.schema.columnMetas[1].offset, 2);  // hasCreator after content (2 bytes)

    // Verify primary keys are preserved
    EXPECT_EQ(projectedTable.primaryKeys.size(), originalTable.primaryKeys.size());
    
    // Verify data integrity - check first row values
    const Row& projectedRow = projectedTable.rows[0];
    auto contentValue = projectedRow.getColumnValue("content", projectedTable.schema);
    auto creatorValue = projectedRow.getColumnValue("hasCreator", projectedTable.schema);
    
    EXPECT_TRUE(std::holds_alternative<std::string>(contentValue));
    EXPECT_EQ(std::get<std::string>(contentValue), "Th");
    
    EXPECT_TRUE(std::holds_alternative<int64_t>(creatorValue));
    EXPECT_EQ(std::get<int64_t>(creatorValue), 1099);
}

TEST_F(CatalogTest, ProjectTable_SingleColumn) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& originalTable = catalog.tables[0];
    
    // Create ThreadPool with size 1
    ThreadPool pool(1);
    
    // Project to single non-primary-key column
    vector<string> projectedColumns = {"content"};
    Table projectedTable = originalTable.project(projectedColumns, pool);
    
    // Verify projected table has requested column
    EXPECT_EQ(projectedTable.schema.columnMetas.size(), 1);
    EXPECT_EQ(projectedTable.schema.columnMetas[0].name, "content");
    EXPECT_EQ(projectedTable.schema.columnMetas[0].type, ColumnType::STRING);
    
    // Verify column offsets are correctly updated for projected schema
    EXPECT_EQ(projectedTable.schema.columnMetas[0].offset, 0);  // content after id (8 bytes)
    
    // Verify data - check all rows
    EXPECT_EQ(projectedTable.rows.size(), 3);
}

TEST_F(CatalogTest, ProjectTable_AllColumns) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& originalTable = catalog.tables[0];
    
    // Create ThreadPool with size 1
    ThreadPool pool(1);
    
    // Project to all columns (should be identical to original)
    vector<string> allColumns = {"id", "content", "hasCreator"};
    Table projectedTable = originalTable.project(allColumns, pool);
    
    // Verify projected table matches original structure
    EXPECT_EQ(projectedTable.schema.columnMetas.size(), originalTable.schema.columnMetas.size());
    EXPECT_EQ(projectedTable.rows.size(), originalTable.rows.size());
    
    // Verify all column names and types match
    for (size_t i = 0; i < allColumns.size(); ++i) {
        EXPECT_EQ(projectedTable.schema.columnMetas[i].name, originalTable.schema.columnMetas[i].name);
        EXPECT_EQ(projectedTable.schema.columnMetas[i].type, originalTable.schema.columnMetas[i].type);
        EXPECT_EQ(projectedTable.schema.columnMetas[i].offset, originalTable.schema.columnMetas[i].offset);
    }
}

TEST_F(CatalogTest, ProjectTable_NonExistentColumn) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& originalTable = catalog.tables[0];
    
    // Create ThreadPool with size 1
    ThreadPool pool(1);
    
    // Try to project with non-existent column
    vector<string> invalidColumns = {"id", "nonexistent", "content"};
    EXPECT_THROW(originalTable.project(invalidColumns, pool), std::runtime_error);
}

TEST_F(CatalogTest, ProjectTable_EmptyColumnList) {
    Catalog catalog;
    catalog.importNodeFromCSV(testCsvPath);
    
    const Table& originalTable = catalog.tables[0];
    
    // Create ThreadPool with size 1
    ThreadPool pool(1);
    
    // Project with empty column list - should only include primary keys
    vector<string> emptyColumns = {};
    Table projectedTable = originalTable.project(emptyColumns, pool);
    
    // Verify projected table has only primary key columns
    EXPECT_EQ(projectedTable.schema.columnMetas.size(), 0);
}

class EdgeCatalogTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary edge CSV file based on Person_knowshort_Person.csv
        testEdgeCsvPath = "Person_knowshort_Person.csv";
        createTestEdgeFile(testEdgeCsvPath);
        
        // Keep track of all test files created for cleanup
        createdTestFiles.push_back(testEdgeCsvPath);
    }

    void TearDown() override {
        // Clean up all test files
        for (const auto& filePath : createdTestFiles) {
            std::filesystem::remove(filePath);
        }
        createdTestFiles.clear();
    }

    void createTestEdgeFile(const std::string& filePath) {
        std::ofstream file(filePath);
        file << "explicitlyDeleted|Person1Id|Person2Id\n";
        file << "boolean|int64|int64\n";
        file << "false|332|2866\n";
        file << "false|332|2869\n";
        file << "false|332|2199023260815\n";
        file << "false|332|10995116284819\n";
        file.close();
    }

    void createTestFile(const std::string& filePath, const std::vector<std::string>& content) {
        std::ofstream file(filePath);
        for (const auto& line : content) {
            file << line << "\n";
        }
        file.close();
        createdTestFiles.push_back(filePath);
    }

    std::string testEdgeCsvPath;
    std::vector<std::string> createdTestFiles;
};

TEST_F(EdgeCatalogTest, ImportEdgeFromCSV_BasicFunctionality) {
    Catalog catalog;
    
    // Import the test edge CSV
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(testEdgeCsvPath));
    
    // Check that two tables were added (forward and reverse)
    EXPECT_EQ(catalog.tables.size(), 2);
    
    // Find forward and reverse tables
    Table* fwdTable = nullptr;
    Table* revTable = nullptr;
    
    for (auto& table : catalog.tables) {
        if (table.name == "knowshort_fwd") {
            fwdTable = &table;
        } else if (table.name == "knowshort_rev") {
            revTable = &table;
        }
    }
    
    ASSERT_NE(fwdTable, nullptr);
    ASSERT_NE(revTable, nullptr);
    
    // Check forward table properties
    EXPECT_EQ(fwdTable->type, TableType::EDGE);
    EXPECT_EQ(fwdTable->rowCount, 4);
    EXPECT_EQ(fwdTable->node_table_names.size(), 2);
    EXPECT_EQ(fwdTable->node_table_names[0], "Person");
    EXPECT_EQ(fwdTable->node_table_names[1], "Person");
    
    // Check reverse table properties  
    EXPECT_EQ(revTable->type, TableType::EDGE);
    EXPECT_EQ(revTable->rowCount, 4);
    EXPECT_EQ(revTable->node_table_names.size(), 2);
    // Note: For reverse table, node_table_names are swapped: {destNodeName, srcNodeName}
    // But in this case both are "Person", so the order doesn't matter visually
    EXPECT_EQ(revTable->node_table_names[0], "Person");
    EXPECT_EQ(revTable->node_table_names[1], "Person");
    
    // Check schema (both tables should have same schema)
    const Schema& schema = fwdTable->schema;
    EXPECT_EQ(schema.columnMetas.size(), 3);
    
    // Check primary keys (should have two for edge tables)
    EXPECT_EQ(fwdTable->primaryKeys.size(), 2);
    EXPECT_EQ(revTable->primaryKeys.size(), 2);
    
    // Verify primary key names for forward table (case-insensitive search should find Person1Id and Person2Id)
    bool foundPerson1Id = false, foundPerson2Id = false;
    for (const auto& pk : fwdTable->primaryKeys) {
        if (pk.name == "Person1Id") foundPerson1Id = true;
        if (pk.name == "Person2Id") foundPerson2Id = true;
    }
    EXPECT_TRUE(foundPerson1Id);
    EXPECT_TRUE(foundPerson2Id);
    
    // Verify primary key order for forward table: Person1Id (srcId), then Person2Id (destId)
    EXPECT_EQ(fwdTable->primaryKeys[0].name, "Person1Id");
    EXPECT_EQ(fwdTable->primaryKeys[1].name, "Person2Id");
    
    // Verify primary key order for reverse table: Person2Id (destId), then Person1Id (srcId)
    EXPECT_EQ(revTable->primaryKeys[0].name, "Person2Id");
    EXPECT_EQ(revTable->primaryKeys[1].name, "Person1Id");
    
    // Check that data is loaded correctly
    EXPECT_EQ(fwdTable->rows.size(), 4);
    EXPECT_EQ(revTable->rows.size(), 4);
    
    // Verify that forward table is sorted by srcId (Person1Id), then destId (Person2Id)
    // All rows should have srcId = 332, so they should be sorted by destId
    auto row0Key = fwdTable->rows[0].key;
    auto row1Key = fwdTable->rows[1].key;
    auto row2Key = fwdTable->rows[2].key;
    auto row3Key = fwdTable->rows[3].key;
    
    EXPECT_EQ(row0Key.first, 332);  // All should have same srcId
    EXPECT_EQ(row1Key.first, 332);
    EXPECT_EQ(row2Key.first, 332);
    EXPECT_EQ(row3Key.first, 332);
    
    // DestId should be in ascending order
    EXPECT_LE(row0Key.second, row1Key.second);
    EXPECT_LE(row1Key.second, row2Key.second);
    EXPECT_LE(row2Key.second, row3Key.second);
    
    // Verify that reverse table is sorted by destId (Person2Id), then srcId (Person1Id)
    // With swapped keys, reverse table has (destId, srcId), so first element is destId
    EXPECT_LE(revTable->rows[0].key.first, revTable->rows[1].key.first);
    EXPECT_LE(revTable->rows[1].key.first, revTable->rows[2].key.first);
    EXPECT_LE(revTable->rows[2].key.first, revTable->rows[3].key.first);
}

TEST_F(EdgeCatalogTest, ImportEdgeFromCSV_DataVerification) {
    Catalog catalog;
    
    // Import the test edge CSV
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(testEdgeCsvPath));
    
    // Find forward and reverse tables
    Table* fwdTable = nullptr;
    Table* revTable = nullptr;
    
    for (auto& table : catalog.tables) {
        if (table.name == "knowshort_fwd") {
            fwdTable = &table;
        } else if (table.name == "knowshort_rev") {
            revTable = &table;
        }
    }
    
    ASSERT_NE(fwdTable, nullptr);
    ASSERT_NE(revTable, nullptr);
    
    // Verify specific data values from the CSV
    // Expected data (sorted by srcId, then destId for forward table):
    // 332|2866, 332|2869, 332|2199023260815, 332|10995116284819
    
    // Check forward table data
    EXPECT_EQ(fwdTable->rows[0].key.first, 332);
    EXPECT_EQ(fwdTable->rows[0].key.second, 2866);
    
    EXPECT_EQ(fwdTable->rows[1].key.first, 332);
    EXPECT_EQ(fwdTable->rows[1].key.second, 2869);
    
    EXPECT_EQ(fwdTable->rows[2].key.first, 332);
    EXPECT_EQ(fwdTable->rows[2].key.second, 2199023260815);
    
    EXPECT_EQ(fwdTable->rows[3].key.first, 332);
    EXPECT_EQ(fwdTable->rows[3].key.second, 10995116284819);
    
    // Check that boolean column is correctly parsed
    auto boolValue0 = fwdTable->rows[0].getColumnValue("explicitlyDeleted", fwdTable->schema);
    auto boolValue1 = fwdTable->rows[1].getColumnValue("explicitlyDeleted", fwdTable->schema);
    
    EXPECT_TRUE(std::holds_alternative<bool>(boolValue0));
    EXPECT_TRUE(std::holds_alternative<bool>(boolValue1));
    EXPECT_FALSE(std::get<bool>(boolValue0));  // Should be false from CSV
    EXPECT_FALSE(std::get<bool>(boolValue1));  // Should be false from CSV
    
    // Check that Person1Id and Person2Id columns have correct values
    auto person1Id = fwdTable->rows[0].getColumnValue("Person1Id", fwdTable->schema);
    auto person2Id = fwdTable->rows[0].getColumnValue("Person2Id", fwdTable->schema);
    
    EXPECT_TRUE(std::holds_alternative<int64_t>(person1Id));
    EXPECT_TRUE(std::holds_alternative<int64_t>(person2Id));
    EXPECT_EQ(std::get<int64_t>(person1Id), 332);
    EXPECT_EQ(std::get<int64_t>(person2Id), 2866);
    
    // Verify reverse table has same data but with swapped keys
    // Forward table keys: (332, destId)
    // Reverse table keys: (destId, 332)
    // Expected data for reverse table (sorted by destId):
    // Should have keys: (2866, 332), (2869, 332), (2199023260815, 332), (10995116284819, 332)
    
    std::vector<obligraph::key_t> expectedDestIds = {2866, 2869, 2199023260815UL, 10995116284819UL};
    std::vector<obligraph::key_t> actualDestIds;
    
    for (const auto& row : revTable->rows) {
        actualDestIds.push_back(row.key.first);  // In reverse table, destId is now first
    }
    
    // Sort the expected dest IDs to match reverse table ordering
    std::sort(expectedDestIds.begin(), expectedDestIds.end());
    
    EXPECT_EQ(actualDestIds.size(), expectedDestIds.size());
    for (size_t i = 0; i < expectedDestIds.size(); i++) {
        EXPECT_EQ(actualDestIds[i], expectedDestIds[i]);
    }
    
    // Verify that reverse table has swapped keys compared to forward table
    std::set<std::pair<obligraph::key_t, obligraph::key_t>> fwdKeys, revKeys;
    for (const auto& row : fwdTable->rows) {
        fwdKeys.insert(row.key);
    }
    for (const auto& row : revTable->rows) {
        // Insert swapped version of reverse table keys to compare with forward
        revKeys.insert({row.key.second, row.key.first});
    }
    
    EXPECT_EQ(fwdKeys, revKeys);  // Forward keys should match swapped reverse keys
}

TEST_F(EdgeCatalogTest, ImportEdgeFromCSV_SchemaValidation) {
    Catalog catalog;
    
    // Import the test edge CSV
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(testEdgeCsvPath));
    
    // Find forward table
    Table* fwdTable = nullptr;
    for (auto& table : catalog.tables) {
        if (table.name == "knowshort_fwd") {
            fwdTable = &table;
            break;
        }
    }
    
    ASSERT_NE(fwdTable, nullptr);
    
    // Verify schema details
    const Schema& schema = fwdTable->schema;
    EXPECT_EQ(schema.columnMetas.size(), 3);
    
    // Check column names and types
    EXPECT_EQ(schema.columnMetas[0].name, "explicitlyDeleted");
    EXPECT_EQ(schema.columnMetas[0].type, ColumnType::BOOLEAN);
    EXPECT_EQ(schema.columnMetas[0].size, 1);  // bool size
    EXPECT_EQ(schema.columnMetas[0].offset, 0);
    
    EXPECT_EQ(schema.columnMetas[1].name, "Person1Id");
    EXPECT_EQ(schema.columnMetas[1].type, ColumnType::INT64);
    EXPECT_EQ(schema.columnMetas[1].size, 8);
    EXPECT_EQ(schema.columnMetas[1].offset, 1);
    
    EXPECT_EQ(schema.columnMetas[2].name, "Person2Id");
    EXPECT_EQ(schema.columnMetas[2].type, ColumnType::INT64);
    EXPECT_EQ(schema.columnMetas[2].size, 8);
    EXPECT_EQ(schema.columnMetas[2].offset, 9);
    
    // Verify total row size
    size_t expectedRowSize = 1 + 8 + 8; // 17 bytes
    EXPECT_EQ(fwdTable->rows[0].size, expectedRowSize);
}

TEST_F(EdgeCatalogTest, ImportEdgeFromCSV_ErrorHandling) {
    Catalog catalog;
    
    // Test with non-existent file
    EXPECT_THROW(catalog.importEdgeFromCSV("non_existent_edge.csv"), std::runtime_error);
    
    // Test with malformed filename (insufficient parts)
    std::string malformedPath = "invalid_filename.csv";
    createTestFile(malformedPath, {
        "col1|col2",
        "int64|int64",
        "1|2"
    });
    
    EXPECT_THROW(catalog.importEdgeFromCSV(malformedPath), std::runtime_error);
    
    // Test with missing primary key columns
    std::string missingKeyPath = "Node1_edge_Node2.csv";
    createTestFile(missingKeyPath, {
        "col1|col2|col3",
        "int64|string|double",
        "1|test|3.14"
    });
    
    EXPECT_THROW(catalog.importEdgeFromCSV(missingKeyPath), std::runtime_error);
}

TEST_F(EdgeCatalogTest, ImportEdgeFromCSV_CaseInsensitiveKeys) {
    // Create a test file with different case primary keys
    std::string caseTestPath = "User_follows_User.csv";
    createTestFile(caseTestPath, {
        "creationDate|USER1ID|USER2ID|isActive",  // Mixed case
        "timestamp|int64|int64|boolean",
        "2023-01-01T00:00:00Z|100|200|true",
        "2023-01-02T00:00:00Z|100|300|false"
    });
    
    Catalog catalog;
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(caseTestPath));
    
    // Should create two tables
    EXPECT_EQ(catalog.tables.size(), 2);
    
    // Find forward table
    Table* fwdTable = nullptr;
    for (auto& table : catalog.tables) {
        if (table.name == "follows_fwd") {
            fwdTable = &table;
            break;
        }
    }
    
    ASSERT_NE(fwdTable, nullptr);
    
    // Verify primary keys were found despite case differences
    EXPECT_EQ(fwdTable->primaryKeys.size(), 2);
    EXPECT_EQ(fwdTable->primaryKeys[0].name, "USER1ID");
    EXPECT_EQ(fwdTable->primaryKeys[1].name, "USER2ID");
    
    // Verify data is correct
    EXPECT_EQ(fwdTable->rows.size(), 2);
    EXPECT_EQ(fwdTable->rows[0].key.first, 100);
    EXPECT_EQ(fwdTable->rows[0].key.second, 200);
    EXPECT_EQ(fwdTable->rows[1].key.first, 100);
    EXPECT_EQ(fwdTable->rows[1].key.second, 300);
    
    // Verify boolean values
    auto isActive0 = fwdTable->rows[0].getColumnValue("isActive", fwdTable->schema);
    auto isActive1 = fwdTable->rows[1].getColumnValue("isActive", fwdTable->schema);
    
    EXPECT_TRUE(std::holds_alternative<bool>(isActive0));
    EXPECT_TRUE(std::holds_alternative<bool>(isActive1));
    EXPECT_TRUE(std::get<bool>(isActive0));   // true from CSV
    EXPECT_FALSE(std::get<bool>(isActive1));  // false from CSV
}

