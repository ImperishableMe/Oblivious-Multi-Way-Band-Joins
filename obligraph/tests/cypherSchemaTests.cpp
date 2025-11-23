#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include "definitions.h"
#include "config.h"
#include "schema_parser.h"

namespace fs = std::filesystem;
using namespace obligraph;

/**
 * Integration tests for Cypher schema support.
 * These tests verify that the system can:
 * 1. Parse Cypher schema files (CREATE NODE TABLE, CREATE REL TABLE)
 * 2. Load CSV data according to schema definitions
 * 3. Execute queries correctly
 *
 * Test Status: These tests are expected to FAIL until implementation is complete.
 */

class CypherSchemaTest : public ::testing::Test {
protected:
    std::string testDataDir = "../../tests/integration/cypher_schema/test_cases/";
    std::string expectedDir = "../../tests/integration/cypher_schema/expected_outputs/";

    void SetUp() override {
        // Verify test data directories exist
        ASSERT_TRUE(fs::exists(testDataDir)) << "Test data directory not found: " << testDataDir;
        ASSERT_TRUE(fs::exists(expectedDir)) << "Expected outputs directory not found: " << expectedDir;
    }

    // Helper: Load expected output from file
    std::string loadExpectedOutput(const std::string& testName) {
        std::string path = expectedDir + testName + "_output.txt";
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open expected output file: " + path);
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        return content;
    }

    // Helper: Load catalog from Cypher schema file
    Catalog loadFromCypherSchema(const std::string& schemaPath, const std::string& dataDir) {
        // Parse schema file
        auto tableDefs = parseCypherSchema(schemaPath);

        // Create catalog
        Catalog catalog;

        // Load each table from CSV
        for (const auto& [tableName, tableDef] : tableDefs) {
            std::string csvPath = dataDir + tableName + ".csv";
            loadTableFromCSV(catalog, tableDef, csvPath);
        }

        return catalog;
    }

    // Helper: Execute OneHopQuery and return result as string
    std::string executeQuery(Catalog& catalog, OneHopQuery& query) {
        // Create thread pool
        ThreadPool pool(4);

        // Execute query using oneHop
        Table result = oneHop(catalog, query, pool);

        // Format output as CSV string
        std::ostringstream output;

        // Detect self-referential join
        bool isSelfReferential = (query.sourceNodeTableName == query.destNodeTableName);

        for (const auto& row : result.rows) {
            std::vector<std::string> values;

            // Extract values for each projected column
            // Use table-qualified column names (e.g., "Person_name", "City_name")
            for (const auto& [tableName, columnName] : query.projectionColumns) {
                std::string qualifiedName;

                // Check if this is an edge column or a node column
                if (tableName == query.edgeTableName) {
                    // Edge columns are not prefixed
                    qualifiedName = columnName;
                } else {
                    // Node columns need table-qualified names
                    std::string tablePrefix = tableName;

                    // In self-referential joins, projections refer to destination by convention
                    if (isSelfReferential && tableName == query.destNodeTableName) {
                        tablePrefix += "_dest";
                    }

                    qualifiedName = tablePrefix + "_" + columnName;
                }

                ColumnValue val = row.getColumnValue(qualifiedName, result.schema);

                // Convert value to string
                std::string strVal;
                if (std::holds_alternative<int32_t>(val)) {
                    strVal = std::to_string(std::get<int32_t>(val));
                } else if (std::holds_alternative<int64_t>(val)) {
                    strVal = std::to_string(std::get<int64_t>(val));
                } else if (std::holds_alternative<std::string>(val)) {
                    strVal = std::get<std::string>(val);
                } else if (std::holds_alternative<double>(val)) {
                    strVal = std::to_string(std::get<double>(val));
                } else if (std::holds_alternative<bool>(val)) {
                    strVal = std::get<bool>(val) ? "true" : "false";
                }

                values.push_back(strVal);
            }

            // Join with commas
            for (size_t i = 0; i < values.size(); i++) {
                if (i > 0) output << ",";
                output << values[i];
            }
            output << "\n";
        }

        return output.str();
    }

    // Helper: Compare actual vs expected output
    void assertOutputMatches(const std::string& actual, const std::string& expected) {
        // Normalize whitespace and compare
        ASSERT_EQ(actual, expected);
    }
};

// ===== Test 1: Simple Two-Node Graph =====
TEST_F(CypherSchemaTest, SimpleGraph_PersonLivesInCity) {
    std::string testDir = testDataDir + "01_simple_graph/";
    std::string schemaPath = testDir + "schema.cypher";

    // Load catalog from Cypher schema
    Catalog catalog = loadFromCypherSchema(schemaPath, testDir);

    // Verify tables loaded
    ASSERT_NO_THROW(catalog.getTable("Person"));
    ASSERT_NO_THROW(catalog.getTable("City"));
    ASSERT_NO_THROW(catalog.getTable("LivesIn_fwd"));
    ASSERT_NO_THROW(catalog.getTable("LivesIn_rev"));

    // Verify row counts
    EXPECT_EQ(catalog.getTable("Person").rowCount, 3);
    EXPECT_EQ(catalog.getTable("City").rowCount, 3);
    EXPECT_EQ(catalog.getTable("LivesIn_fwd").rowCount, 3);

    // Execute query: Find where Alice (id=1) lives
    OneHopQuery query(
        "Person",           // source node
        "LivesIn",          // edge
        "City",             // dest node
        {{"Person", {{"id", Predicate::Cmp::EQ, int64_t(1)}}}},  // filter: Person.id = 1
        {{"Person", "name"}, {"City", "name"}}                    // projection
    );

    std::string result = executeQuery(catalog, query);
    std::string expected = loadExpectedOutput("test_01");

    assertOutputMatches(result, expected);
}

// ===== Test 2: Self-Referential Relationship =====
TEST_F(CypherSchemaTest, SelfReferential_UserFollowsUser) {
    std::string testDir = testDataDir + "02_self_referential/";
    std::string schemaPath = testDir + "schema.cypher";

    Catalog catalog = loadFromCypherSchema(schemaPath, testDir);

    // Verify User table loaded
    ASSERT_NO_THROW(catalog.getTable("User"));
    EXPECT_EQ(catalog.getTable("User").rowCount, 4);

    // Execute query: Find who Alice (id=1) follows
    OneHopQuery query(
        "User",
        "Follows",
        "User",
        {{"User", {{"id", Predicate::Cmp::EQ, int64_t(1)}}}},
        {{"User", "name"}}  // destination user name
    );

    std::string result = executeQuery(catalog, query);
    std::string expected = loadExpectedOutput("test_02");

    assertOutputMatches(result, expected);
}

// ===== Test 3: Multi-Relationship Graph =====
TEST_F(CypherSchemaTest, MultiRelationship_WorksAtAndManages) {
    std::string testDir = testDataDir + "03_multi_relationship/";
    std::string schemaPath = testDir + "schema.cypher";

    Catalog catalog = loadFromCypherSchema(schemaPath, testDir);

    // Verify all tables loaded
    ASSERT_NO_THROW(catalog.getTable("Person"));
    ASSERT_NO_THROW(catalog.getTable("Company"));
    ASSERT_NO_THROW(catalog.getTable("WorksAt_fwd"));
    ASSERT_NO_THROW(catalog.getTable("WorksAt_rev"));
    ASSERT_NO_THROW(catalog.getTable("Manages_fwd"));
    ASSERT_NO_THROW(catalog.getTable("Manages_rev"));

    // Test 3a: WorksAt query (Alice works where?)
    OneHopQuery worksAtQuery(
        "Person",
        "WorksAt",
        "Company",
        {{"Person", {{"id", Predicate::Cmp::EQ, int64_t(1)}}}},
        {{"Person", "name"}, {"Company", "name"}, {"WorksAt", "role"}}
    );

    std::string worksAtResult = executeQuery(catalog, worksAtQuery);
    std::string worksAtExpected = loadExpectedOutput("test_03_worksAt");
    assertOutputMatches(worksAtResult, worksAtExpected);

    // Test 3b: Manages query (Bob manages whom?)
    OneHopQuery managesQuery(
        "Person",
        "Manages",
        "Person",
        {{"Person", {{"id", Predicate::Cmp::EQ, int64_t(2)}}}},
        {{"Person", "name"}}  // managed person's name
    );

    std::string managesResult = executeQuery(catalog, managesQuery);
    std::string managesExpected = loadExpectedOutput("test_03_manages");
    assertOutputMatches(managesResult, managesExpected);
}

// ===== Test 4: All Data Types =====
TEST_F(CypherSchemaTest, AllTypes_ProductSimilarity) {
    std::string testDir = testDataDir + "04_all_types/";
    std::string schemaPath = testDir + "schema.cypher";

    Catalog catalog = loadFromCypherSchema(schemaPath, testDir);

    // Verify Product table has all type columns
    Table& productTable = catalog.getTable("Product");
    EXPECT_EQ(productTable.schema.columnMetas.size(), 8);  // 8 columns with different types

    // Execute query with BOOLEAN filter
    OneHopQuery query(
        "Product",
        "Similar",
        "Product",
        {{"Product", {{"available", Predicate::Cmp::EQ, true}}}},
        {{"Product", "name"}, {"Similar", "score"}}
    );

    std::string result = executeQuery(catalog, query);
    std::string expected = loadExpectedOutput("test_04");

    assertOutputMatches(result, expected);
}

// ===== Test 5: Empty Tables Edge Case =====
TEST_F(CypherSchemaTest, EmptyTables_NoRelationships) {
    std::string testDir = testDataDir + "05_empty_tables/";
    std::string schemaPath = testDir + "schema.cypher";

    Catalog catalog = loadFromCypherSchema(schemaPath, testDir);

    // Verify tables exist but LivesIn is empty
    EXPECT_EQ(catalog.getTable("Person").rowCount, 2);
    EXPECT_EQ(catalog.getTable("City").rowCount, 1);
    // LivesIn table should exist but be empty

    // Query should return empty result
    OneHopQuery query(
        "Person",
        "LivesIn",
        "City",
        {},
        {{"Person", "name"}, {"City", "name"}}
    );

    std::string result = executeQuery(catalog, query);
    std::string expected = loadExpectedOutput("test_05");

    // Both should be empty
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(expected.empty());
}

// ===== Test 6: CLI Argument Handling =====
// These would be integration tests run via shell script, not unit tests
// Skipping for now as they require actual executable

// ===== Test 7: Error Detection =====
TEST_F(CypherSchemaTest, ErrorDetection_MissingPrimaryKey) {
    std::string testDir = testDataDir + "07_error_handling/missing_pk/";
    std::string schemaPath = testDir + "schema.cypher";

    // Should throw error when parsing schema without PRIMARY KEY
    EXPECT_THROW(
        loadFromCypherSchema(schemaPath, testDir),
        std::runtime_error
    );
}

TEST_F(CypherSchemaTest, ErrorDetection_MissingTO) {
    std::string testDir = testDataDir + "07_error_handling/missing_to/";
    std::string schemaPath = testDir + "schema.cypher";

    // Should throw error when REL TABLE missing TO clause
    EXPECT_THROW(
        loadFromCypherSchema(schemaPath, testDir),
        std::runtime_error
    );
}

TEST_F(CypherSchemaTest, ErrorDetection_WrongColumnCount) {
    std::string testDir = testDataDir + "07_error_handling/wrong_column_count/";
    std::string schemaPath = testDir + "schema.cypher";

    // Should throw error when CSV has wrong number of columns
    EXPECT_THROW(
        loadFromCypherSchema(schemaPath, testDir),
        std::runtime_error
    );
}

TEST_F(CypherSchemaTest, ErrorDetection_InvalidPKColumn) {
    std::string testDir = testDataDir + "07_error_handling/invalid_pk_column/";
    std::string schemaPath = testDir + "schema.cypher";

    // Should throw error when PRIMARY KEY references non-existent column
    EXPECT_THROW(
        loadFromCypherSchema(schemaPath, testDir),
        std::runtime_error
    );
}

// ===== Test 8: Large Dataset Performance =====
TEST_F(CypherSchemaTest, LargeDataset_10kUsers50kEdges) {
    std::string testDir = testDataDir + "08_large_dataset/";
    std::string schemaPath = testDir + "schema.cypher";

    // Load large dataset
    auto startLoad = std::chrono::high_resolution_clock::now();
    Catalog catalog = loadFromCypherSchema(schemaPath, testDir);
    auto endLoad = std::chrono::high_resolution_clock::now();

    // Verify data loaded
    EXPECT_EQ(catalog.getTable("User").rowCount, 10000);

    // Execute query
    OneHopQuery query(
        "User",
        "Follows",
        "User",
        {{"User", {{"id", Predicate::Cmp::LT, int64_t(100)}}}},
        {{"User", "id"}}
    );

    auto startQuery = std::chrono::high_resolution_clock::now();
    std::string result = executeQuery(catalog, query);
    auto endQuery = std::chrono::high_resolution_clock::now();

    // Performance assertions (adjust thresholds as needed)
    auto loadTime = std::chrono::duration_cast<std::chrono::milliseconds>(endLoad - startLoad).count();
    auto queryTime = std::chrono::duration_cast<std::chrono::milliseconds>(endQuery - startQuery).count();

    std::cout << "Load time: " << loadTime << "ms" << std::endl;
    std::cout << "Query time: " << queryTime << "ms" << std::endl;

    // Just verify it completes without crashing
    EXPECT_FALSE(result.empty());
}
