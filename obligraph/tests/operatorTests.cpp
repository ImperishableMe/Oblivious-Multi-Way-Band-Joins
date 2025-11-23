#include <gtest/gtest.h>
#include "../include/definitions.h"
#include <fstream>
#include <filesystem>
#include <variant>

using namespace obligraph;

class OperatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test CSV file with diverse data for filtering
        testCsvPath = "test_operator_data.csv";
        std::ofstream file(testCsvPath);
        file << "id|name|age|salary|active\n";
        file << "int64|string|int32|double|boolean\n";
        file << "1|Alice|25|50000.0|true\n";
        file << "2|Bob|30|60000.0|false\n";
        file << "3|Charlie|35|75000.0|true\n";
        file << "4|Diana|28|55000.0|true\n";
        file << "5|Eve|40|80000.0|false\n";
        file.close();
    }

    void TearDown() override {
        // Clean up test file
        std::filesystem::remove(testCsvPath);
    }

    std::string testCsvPath;
    ThreadPool pool{4};  // Thread pool for parallel operations
};

TEST_F(OperatorTest, FilterAndProject_BasicFunctionality) {
    Catalog catalog;
    
    // Import the test CSV
    ASSERT_NO_THROW(catalog.importNodeFromCSV(testCsvPath));
    
    // Get the imported table and make a copy for filtering
    ASSERT_EQ(catalog.tables.size(), 1);
    Table filteredTable = catalog.tables[0];  // Copy the original table
    
    // Verify initial data
    EXPECT_EQ(filteredTable.name, "test_operator_data");
    EXPECT_EQ(filteredTable.type, TableType::NODE);
    EXPECT_EQ(filteredTable.rowCount, 5);
    EXPECT_EQ(filteredTable.rows.size(), 5);
    
    // Create filter predicates to filter for active employees (active == true)
    vector<Predicate> predicates;
    Predicate activePredicate;
    activePredicate.column = "active";
    activePredicate.op = Predicate::Cmp::EQ;
    activePredicate.constant = true;
    predicates.push_back(activePredicate);
    
    // Apply filter operation in-place with threadpool
    ThreadPool pool(4);
    filteredTable.filter(predicates, pool);
    
    // Verify filtered results - should have 3 rows (Alice, Charlie, Diana)
    EXPECT_EQ(filteredTable.rowCount, 3);
    EXPECT_EQ(filteredTable.rows.size(), 3);
    EXPECT_EQ(filteredTable.name, "test_operator_data");  // Name doesn't change with in-place filter
    EXPECT_EQ(filteredTable.type, TableType::NODE);
    
    // Verify that all filtered rows have active == true
    for (const auto& row : filteredTable.rows) {
        auto activeValue = row.getColumnValue("active", filteredTable.schema);
        EXPECT_TRUE(std::holds_alternative<bool>(activeValue));
        EXPECT_TRUE(std::get<bool>(activeValue));
    }
    
    // Verify specific filtered row IDs (should be 1, 3, 4 for Alice, Charlie, Diana)
    auto row0Id = filteredTable.rows[0].getColumnValue("id", filteredTable.schema);
    auto row1Id = filteredTable.rows[1].getColumnValue("id", filteredTable.schema);
    auto row2Id = filteredTable.rows[2].getColumnValue("id", filteredTable.schema);
    
    // Get the IDs and sort them to check (filter might not preserve order)
    vector<int64_t> filteredIds;
    filteredIds.push_back(std::get<int64_t>(row0Id));
    filteredIds.push_back(std::get<int64_t>(row1Id));
    filteredIds.push_back(std::get<int64_t>(row2Id));
    sort(filteredIds.begin(), filteredIds.end());
    
    EXPECT_EQ(filteredIds[0], 1);  // Alice
    EXPECT_EQ(filteredIds[1], 3);  // Charlie
    EXPECT_EQ(filteredIds[2], 4);  // Diana
    
    // Now apply projection to the filtered table - select only name and salary
    vector<string> projectedColumns = {"name", "salary"};
    Table finalTable = filteredTable.project(projectedColumns, pool);
    
    // Verify final projected table
    EXPECT_EQ(finalTable.name, filteredTable.name + "_projected");
    EXPECT_EQ(finalTable.type, filteredTable.type);
    EXPECT_EQ(finalTable.rowCount, 3);
    EXPECT_EQ(finalTable.rows.size(), 3);
    
    // Verify projected schema requested columns
    EXPECT_EQ(finalTable.schema.columnMetas.size(), 2);
    EXPECT_EQ(finalTable.schema.columnMetas[0].name, "name");
    EXPECT_EQ(finalTable.schema.columnMetas[1].name, "salary");
    
    // Verify column types are preserved
    EXPECT_EQ(finalTable.schema.columnMetas[0].type, ColumnType::STRING);
    EXPECT_EQ(finalTable.schema.columnMetas[1].type, ColumnType::DOUBLE);
    
    // Verify data integrity in final table
    for (size_t i = 0; i < finalTable.rows.size(); i++) {
        auto nameValue = finalTable.rows[i].getColumnValue("name", finalTable.schema);
        auto salaryValue = finalTable.rows[i].getColumnValue("salary", finalTable.schema);

        EXPECT_TRUE(std::holds_alternative<string>(nameValue));
        EXPECT_TRUE(std::holds_alternative<double>(salaryValue));
        
        string name = std::get<string>(nameValue);
        double salary = std::get<double>(salaryValue);
        
        // Verify the name and salary match expected values for active employees
        if (i == 0) {
            EXPECT_EQ(name, "Al");
            EXPECT_EQ(salary, 50000.0);
        } else if (i == 1) {
            EXPECT_EQ(name, "Ch");
            EXPECT_EQ(salary, 75000.0);
        } else if (i == 2) {
            EXPECT_EQ(name, "Di");
            EXPECT_EQ(salary, 55000.0);
        }
    }
}

TEST_F(OperatorTest, FilterNumericPredicate_GreaterThan) {
    Catalog catalog;
    
    // Import the test CSV
    ASSERT_NO_THROW(catalog.importNodeFromCSV(testCsvPath));
    
    Table filteredTable = catalog.tables[0];  // Copy the original table
    
    // Create filter predicate for age > 30
    vector<Predicate> predicates;
    Predicate agePredicate;
    agePredicate.column = "age";
    agePredicate.op = Predicate::Cmp::GT;
    agePredicate.constant = int32_t(30);
    predicates.push_back(agePredicate);
    
    // Apply filter operation in-place with threadpool
    ThreadPool pool(4);
    filteredTable.filter(predicates, pool);
    
    // Should have 2 rows (Charlie: 35, Eve: 40)
    EXPECT_EQ(filteredTable.rowCount, 2);
    EXPECT_EQ(filteredTable.rows.size(), 2);
    
    // Verify that all filtered rows have age > 30
    for (const auto& row : filteredTable.rows) {
        auto ageValue = row.getColumnValue("age", filteredTable.schema);
        EXPECT_TRUE(std::holds_alternative<int32_t>(ageValue));
        EXPECT_GT(std::get<int32_t>(ageValue), 30);
    }
    
    // Project to just name and age
    vector<string> projectedColumns = {"name", "age"};
    Table finalTable = filteredTable.project(projectedColumns, pool);
    
    // Verify final results
    EXPECT_EQ(finalTable.rowCount, 2);
    EXPECT_EQ(finalTable.schema.columnMetas.size(), 2); // name + age
    
    // Check specific values
    vector<pair<string, int32_t>> expectedResults = {{"Ch", 35}, {"Ev", 40}};
    vector<pair<string, int32_t>> actualResults;
    
    for (const auto& row : finalTable.rows) {
        auto nameValue = row.getColumnValue("name", finalTable.schema);
        auto ageValue = row.getColumnValue("age", finalTable.schema);
        
        string name = std::get<string>(nameValue);
        int32_t age = std::get<int32_t>(ageValue);
        actualResults.push_back({name, age});
    }
    
    // Sort both vectors for comparison
    sort(expectedResults.begin(), expectedResults.end());
    sort(actualResults.begin(), actualResults.end());
    
    EXPECT_EQ(actualResults, expectedResults);
}

TEST_F(OperatorTest, FilterSalaryAndProject_ComplexPipeline) {
    Catalog catalog;
    
    // Import the test CSV
    ASSERT_NO_THROW(catalog.importNodeFromCSV(testCsvPath));
    
    Table filteredTable = catalog.tables[0];  // Copy the original table
    
    // Create filter predicate for salary >= 60000
    vector<Predicate> predicates;
    Predicate salaryPredicate;
    salaryPredicate.column = "salary";
    salaryPredicate.op = Predicate::Cmp::GTE;
    salaryPredicate.constant = 60000.0;
    predicates.push_back(salaryPredicate);
    
    // Apply filter operation in-place with threadpool
    ThreadPool pool(4);
    filteredTable.filter(predicates, pool);
    
    // Should have 3 rows (Bob: 60000, Charlie: 75000, Eve: 80000)
    EXPECT_EQ(filteredTable.rowCount, 3);
    EXPECT_EQ(filteredTable.rows.size(), 3);
    
    // Verify that all filtered rows have salary >= 60000
    for (const auto& row : filteredTable.rows) {
        auto salaryValue = row.getColumnValue("salary", filteredTable.schema);
        EXPECT_TRUE(std::holds_alternative<double>(salaryValue));
        EXPECT_GE(std::get<double>(salaryValue), 60000.0);
    }
    
    // Project to only include id and name (minimal projection)
    vector<string> projectedColumns = {"name"};
    Table finalTable = filteredTable.project(projectedColumns, pool);
    
    // Verify final results
    EXPECT_EQ(finalTable.rowCount, 3);
    EXPECT_EQ(finalTable.schema.columnMetas.size(), 1); // name only
    EXPECT_EQ(finalTable.schema.columnMetas[0].name, "name");
    
    // Check that we have the right names
    vector<string> expectedNames = {"Bo", "Ch", "Ev"};
    vector<string> actualNames;
    
    for (const auto& row : finalTable.rows) {
        auto nameValue = row.getColumnValue("name", finalTable.schema);
        actualNames.push_back(std::get<string>(nameValue));
    }
    
    sort(expectedNames.begin(), expectedNames.end());
    sort(actualNames.begin(), actualNames.end());
    
    EXPECT_EQ(actualNames, expectedNames);
}

TEST_F(OperatorTest, EmptyFilterResult_ThenProject) {
    Catalog catalog;
    
    // Import the test CSV
    ASSERT_NO_THROW(catalog.importNodeFromCSV(testCsvPath));
    
    Table filteredTable = catalog.tables[0];  // Copy the original table
    
    // Create filter predicate that matches no rows (age > 50)
    vector<Predicate> predicates;
    Predicate agePredicate;
    agePredicate.column = "age";
    agePredicate.op = Predicate::Cmp::GT;
    agePredicate.constant = int32_t(50);
    predicates.push_back(agePredicate);
    
    // Apply filter operation in-place with threadpool
    ThreadPool pool(4);
    filteredTable.filter(predicates, pool);
    
    // Should have 0 rows
    EXPECT_EQ(filteredTable.rowCount, 0);
    EXPECT_EQ(filteredTable.rows.size(), 0);
    
    // Project the empty table
    vector<string> projectedColumns = {"name", "salary"};
    Table finalTable = filteredTable.project(projectedColumns, pool);
    
    // Should still have 0 rows but correct schema
    EXPECT_EQ(finalTable.rowCount, 0);
    EXPECT_EQ(finalTable.rows.size(), 0);
    EXPECT_EQ(finalTable.schema.columnMetas.size(), 2); // name + salary
}

TEST_F(OperatorTest, UnionOperator_DisjointSchemas) {
    // Test union of two tables with completely different schemas
    Catalog catalog;
    
    // Create first table with employee data  
    std::string firstCsvPath = "test_union_first.csv";
    std::ofstream file1(firstCsvPath);
    file1 << "emp_id|name\n";
    file1 << "int64|string\n";
    file1 << "1|Alice\n";
    file1 << "2|Bob\n";
    file1.close();
    
    // Create second table with salary data (same number of rows)
    std::string secondCsvPath = "test_union_second.csv";
    std::ofstream file2(secondCsvPath);
    file2 << "salary_id|salary\n";
    file2 << "int64|double\n";
    file2 << "1|50000.0\n";
    file2 << "2|60000.0\n";
    file2.close();
    
    // Import both tables
    ASSERT_NO_THROW(catalog.importNodeFromCSV(firstCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(secondCsvPath));
    
    ASSERT_EQ(catalog.tables.size(), 2);
    Table& firstTable = catalog.tables[0];
    const Table& secondTable = catalog.tables[1];
    
    // Verify initial state
    EXPECT_EQ(firstTable.rowCount, 2);
    EXPECT_EQ(secondTable.rowCount, 2);
    EXPECT_EQ(firstTable.schema.columnMetas.size(), 2); // emp_id, name
    EXPECT_EQ(secondTable.schema.columnMetas.size(), 2); // salary_id, salary

    // Perform union operation
    firstTable.unionWith(secondTable, pool);
    
    // Verify expanded schema includes all columns
    EXPECT_EQ(firstTable.schema.columnMetas.size(), 4); // All columns from both tables
    
    // Verify column names include all from both tables
    std::set<std::string> columnNames;
    for (const auto& meta : firstTable.schema.columnMetas) {
        columnNames.insert(meta.name);
    }
    EXPECT_TRUE(columnNames.count("emp_id"));
    EXPECT_TRUE(columnNames.count("name"));
    EXPECT_TRUE(columnNames.count("salary_id"));
    EXPECT_TRUE(columnNames.count("salary"));
    
    // Verify row count remains the same
    EXPECT_EQ(firstTable.rowCount, 2);
    
    // Clean up
    std::filesystem::remove(firstCsvPath);
    std::filesystem::remove(secondCsvPath);
}

TEST_F(OperatorTest, UnionOperator_OverlappingSchemas) {
    // Test union where both tables have some common columns
    Catalog catalog;
    
    // Create first table
    std::string firstCsvPath = "test_union_overlap1.csv";
    std::ofstream file1(firstCsvPath);
    file1 << "id|name|age\n";
    file1 << "int64|string|int32\n";
    file1 << "1|Alice|25\n";
    file1 << "2|Bob|30\n";
    file1.close();
    
    // Create second table with overlapping columns (id and name) and new column (salary)
    std::string secondCsvPath = "test_union_overlap2.csv";
    std::ofstream file2(secondCsvPath);
    file2 << "id|name|salary\n";
    file2 << "int64|string|double\n";
    file2 << "1|Different_Alice|50000.0\n";
    file2 << "2|Different_Bob|60000.0\n";
    file2.close();
    
    // Import both tables
    ASSERT_NO_THROW(catalog.importNodeFromCSV(firstCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(secondCsvPath));
    
    Table& firstTable = catalog.tables[0];
    const Table& secondTable = catalog.tables[1];
    
    // Store original values from first table to verify they're preserved
    ColumnValue originalName1 = firstTable.rows[0].getColumnValue("name", firstTable.schema);
    ColumnValue originalAge1 = firstTable.rows[0].getColumnValue("age", firstTable.schema);
    
    // Perform union operation
    firstTable.unionWith(secondTable, pool);
    
    // Verify the result includes all unique columns
    std::set<std::string> columnNames;
    for (const auto& meta : firstTable.schema.columnMetas) {
        columnNames.insert(meta.name);
    }
    EXPECT_TRUE(columnNames.count("id"));
    EXPECT_TRUE(columnNames.count("name"));
    EXPECT_TRUE(columnNames.count("age"));
    EXPECT_TRUE(columnNames.count("salary"));
    EXPECT_EQ(firstTable.schema.columnMetas.size(), 4); // id, name, age, salary
    
    // Verify that original table's columns are preserved (first table wins conflicts)
    ColumnValue preservedName1 = firstTable.rows[0].getColumnValue("name", firstTable.schema);
    ColumnValue preservedAge1 = firstTable.rows[0].getColumnValue("age", firstTable.schema);
    
    EXPECT_EQ(std::get<std::string>(preservedName1), std::get<std::string>(originalName1));
    EXPECT_EQ(std::get<int32_t>(preservedAge1), std::get<int32_t>(originalAge1));
    
    // Verify new column (salary) is added from second table
    ColumnValue newSalary1 = firstTable.rows[0].getColumnValue("salary", firstTable.schema);
    EXPECT_EQ(std::get<double>(newSalary1), 50000.0);
    
    // Clean up
    std::filesystem::remove(firstCsvPath);
    std::filesystem::remove(secondCsvPath);
}

TEST_F(OperatorTest, UnionOperator_SameSchemas) {
    // Test union where both tables have identical schemas
    Catalog catalog;
    
    // Create first table
    std::string firstCsvPath = "test_union_same1.csv";
    std::ofstream file1(firstCsvPath);
    file1 << "id|name|age\n";
    file1 << "int64|string|int32\n";
    file1 << "1|Alice|25\n";
    file1 << "2|Bob|30\n";
    file1.close();
    
    // Create second table with identical schema but different data
    std::string secondCsvPath = "test_union_same2.csv";
    std::ofstream file2(secondCsvPath);
    file2 << "id|name|age\n";
    file2 << "int64|string|int32\n";
    file2 << "1|Different_Alice|99\n";
    file2 << "2|Different_Bob|99\n";
    file2.close();
    
    // Import both tables
    ASSERT_NO_THROW(catalog.importNodeFromCSV(firstCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(secondCsvPath));
    
    Table& firstTable = catalog.tables[0];
    const Table& secondTable = catalog.tables[1];
    
    // Store original values to verify they're preserved
    ColumnValue originalName1 = firstTable.rows[0].getColumnValue("name", firstTable.schema);
    ColumnValue originalAge1 = firstTable.rows[0].getColumnValue("age", firstTable.schema);
    
    // Perform union operation
    firstTable.unionWith(secondTable, pool);
    
    // Schema should remain the same size (no new columns)
    EXPECT_EQ(firstTable.schema.columnMetas.size(), 3); // id, name, age
    
    // Data from first table should be preserved (first table wins)
    ColumnValue preservedName1 = firstTable.rows[0].getColumnValue("name", firstTable.schema);
    ColumnValue preservedAge1 = firstTable.rows[0].getColumnValue("age", firstTable.schema);
    
    EXPECT_EQ(std::get<std::string>(preservedName1), "Al");
    EXPECT_EQ(std::get<int32_t>(preservedAge1), 25);
    
    // Clean up
    std::filesystem::remove(firstCsvPath);
    std::filesystem::remove(secondCsvPath);
}

TEST_F(OperatorTest, UnionOperator_EmptyTables) {
    // Test union with empty tables
    Table table1, table2;
    
    // Set up minimal schemas for empty tables
    table1.name = "empty1";
    table1.type = TableType::NODE;
    table1.rowCount = 0;
    
    table2.name = "empty2";
    table2.type = TableType::NODE;
    table2.rowCount = 0;
    
    // Add some column metadata to make it interesting
    ColumnMeta col1 = {"id", ColumnType::INT64, sizeof(int64_t), 0};
    ColumnMeta col2 = {"data", ColumnType::STRING, 0, sizeof(int64_t)};
    
    table1.schema.columnMetas = {col1};
    table2.schema.columnMetas = {col2};
    
    // Perform union
    table1.unionWith(table2, pool);
    
    // Should still be empty but with expanded schema
    EXPECT_EQ(table1.rowCount, 0);
    EXPECT_EQ(table1.schema.columnMetas.size(), 2); // id + data
}
