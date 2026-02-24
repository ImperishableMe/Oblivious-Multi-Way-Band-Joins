#include <gtest/gtest.h>
#include "../include/definitions.h"
#include <fstream>
#include <filesystem>
#include <variant>
#include <set>
#include <map>

using namespace obligraph;

class OneHopTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test CSV files for nodes and edges
        createPersonNodeCSV();
        createOrgNodeCSV();
        createWorksAtEdgeCSV();
    }

    void TearDown() override {
        // Clean up test files
        std::filesystem::remove(personCsvPath);
        std::filesystem::remove(orgCsvPath);
        std::filesystem::remove(worksAtCsvPath);
    }

    void createPersonNodeCSV() {
        personCsvPath = "person.csv";
        std::ofstream file(personCsvPath);
        file << "id|age|first_name|last_name\n";
        file << "int64|int32|int32|int32\n";
        file << "1|30|11|21\n";    // Alice Johnson
        file << "2|22|12|22\n";    // Bob Smith
        file << "3|28|13|23\n";    // Charlie Brown
        file << "4|35|14|24\n";    // Diana Wilson
        file << "5|40|15|25\n";    // Eve Davis
        file << "6|24|16|26\n";    // Frank Miller
        file.close();
    }

    void createOrgNodeCSV() {
        orgCsvPath = "org.csv";
        std::ofstream file(orgCsvPath);
        file << "id|establishedAt|name|city_name\n";
        file << "int64|int32|int32|int32\n";
        file << "101|2010|201|301\n";    // TechCorp, waterloo
        file << "102|2015|202|302\n";    // DataSoft, toronto
        file << "103|2008|203|301\n";    // InnovateInc, waterloo
        file << "104|2020|204|303\n";    // StartupXYZ, vancouver
        file << "105|2012|205|301\n";    // BigTech, waterloo
        file.close();
    }

    void createWorksAtEdgeCSV() {
        worksAtCsvPath = "person_worksAt_org.csv";
        std::ofstream file(worksAtCsvPath);
        file << "personId|orgId|since\n";
        file << "int64|int64|int32\n";
        file << "1|101|5\n";      // Alice works at TechCorp for 5 years (waterloo)
        file << "2|102|1\n";      // Bob works at DataSoft for 1 year (toronto)
        file << "3|103|3\n";      // Charlie works at InnovateInc for 3 years (waterloo)
        file << "4|104|2\n";      // Diana works at StartupXYZ for 2 years (vancouver)
        file << "5|105|6\n";      // Eve works at BigTech for 6 years (waterloo)
        file << "6|102|4\n";      // Frank works at DataSoft for 4 years (toronto)
        file.close();
    }

    std::string personCsvPath;
    std::string orgCsvPath;
    std::string worksAtCsvPath;
};

TEST_F(OneHopTest, OneHopCypherQuery_FilterAndProject) {
    Catalog catalog;

    // Import all test CSV files
    ASSERT_NO_THROW(catalog.importNodeFromCSV(personCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(orgCsvPath));
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(worksAtCsvPath));

    // Verify that all tables were imported correctly
    ASSERT_EQ(catalog.tables.size(), 4); // person, org, worksAt_fwd, worksAt_rev

    // Find the tables we need
    Table* personTable = nullptr;
    Table* orgTable = nullptr;
    Table* worksAtTable = nullptr;

    for (auto& table : catalog.tables) {
        if (table.name == "person") {
            personTable = &table;
        } else if (table.name == "org") {
            orgTable = &table;
        } else if (table.name == "worksAt_fwd") {
            worksAtTable = &table;
        }
    }

    ASSERT_NE(personTable, nullptr);
    ASSERT_NE(orgTable, nullptr);
    ASSERT_NE(worksAtTable, nullptr);

    // Verify initial data counts
    EXPECT_EQ(personTable->rowCount, 6);
    EXPECT_EQ(orgTable->rowCount, 5);
    EXPECT_EQ(worksAtTable->rowCount, 6);

    // Create predicates for the one-hop query:
    // 1. Person age > 25
    // 2. Org city_name == "waterloo"
    // Note: Edge predicates are not currently supported (column naming mismatch)

    vector<Predicate> personPredicates;
    Predicate ageFilter;
    ageFilter.column = "age";
    ageFilter.op = Predicate::Cmp::GT;
    ageFilter.constant = int32_t(25);
    personPredicates.push_back(ageFilter);

    vector<Predicate> orgPredicates;
    Predicate cityFilter;
    cityFilter.column = "city_name";
    cityFilter.op = Predicate::Cmp::EQ;
    cityFilter.constant = int32_t(301);  // waterloo = 301
    orgPredicates.push_back(cityFilter);

    // Combine predicates with table names (no edge predicates)
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates},
        {"org", orgPredicates}
    };

    // Define projection columns with table names - we want person first_name and org name
    vector<pair<string, string>> projectionColumns = {
        {"person", "first_name"},
        {"org", "name"}
    };

    // Create OneHopQuery struct with table names and combined predicates
    OneHopQuery query("person", "worksAt", "org",
                      tablePredicates, projectionColumns);

    // Call the oneHop function
    ThreadPool pool(1);
    Table result = oneHop(catalog, query, pool);

    // Column names are prefixed with table names in the implementation
    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "person_first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "org_name");

    // Expected results based on our test data:
    // - Alice (age 30 > 25) works at TechCorp (waterloo=301) ✓
    // - Charlie (age 28 > 25) works at InnovateInc (waterloo=301) ✓
    // - Eve (age 40 > 25) works at BigTech (waterloo=301) ✓
    //
    // Filtered out:
    // - Bob (age 22 ≤ 25)
    // - Diana (age 35 > 25) works at StartupXYZ (vancouver=303 ≠ 301)
    // - Frank (age 24 ≤ 25)

    EXPECT_EQ(result.rowCount, 3);
    // Collect all result names (order may vary)
    // first_name IDs: Alice=11, Charlie=13, Eve=15
    // org name IDs: TechCorp=201, InnovateInc=203, BigTech=205
    std::set<int32_t> expectedFirstNames = {11, 13, 15};
    std::set<int32_t> actualFirstNames;
    std::map<int32_t, int32_t> expectedCompanies = {
        {11, 201},   // Alice -> TechCorp
        {13, 203},   // Charlie -> InnovateInc
        {15, 205}    // Eve -> BigTech
    };

    for (size_t i = 0; i < result.rowCount; i++) {
        auto firstName = std::get<int32_t>(result.rows[i].getColumnValue("person_first_name", result.schema));
        auto companyName = std::get<int32_t>(result.rows[i].getColumnValue("org_name", result.schema));
        actualFirstNames.insert(firstName);

        if (expectedCompanies.count(firstName)) {
            EXPECT_EQ(companyName, expectedCompanies[firstName]);
        }
    }
    EXPECT_EQ(actualFirstNames, expectedFirstNames);
}

TEST_F(OneHopTest, OneHopCypherQuery_NoResults) {
    Catalog catalog;

    // Import all test CSV files
    ASSERT_NO_THROW(catalog.importNodeFromCSV(personCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(orgCsvPath));
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(worksAtCsvPath));

    // Find the tables we need
    Table* personTable = nullptr;
    Table* orgTable = nullptr;
    Table* worksAtTable = nullptr;

    for (auto& table : catalog.tables) {
        if (table.name == "person") {
            personTable = &table;
        } else if (table.name == "org") {
            orgTable = &table;
        } else if (table.name == "worksAt_fwd") {
            worksAtTable = &table;
        }
    }

    ASSERT_NE(personTable, nullptr);
    ASSERT_NE(orgTable, nullptr);
    ASSERT_NE(worksAtTable, nullptr);

    // Create very restrictive predicates that should match no results:
    // Person age > 50 (no one in our data is over 50)

    vector<Predicate> personPredicates;
    Predicate ageFilter;
    ageFilter.column = "age";
    ageFilter.op = Predicate::Cmp::GT;
    ageFilter.constant = int32_t(50);
    personPredicates.push_back(ageFilter);

    // Combine predicates with table names (only person predicates, others are empty)
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates}
    };

    // Define projection columns with table names
    vector<pair<string, string>> projectionColumns = {
        {"person", "first_name"},
        {"org", "name"}
    };

    // Create OneHopQuery struct with table names and combined predicates
    OneHopQuery query("person", "worksAt", "org",
                      tablePredicates, projectionColumns);

    // Call the oneHop function
    auto pool = ThreadPool(1);
    Table result = oneHop(catalog, query, pool);

    // Verify that no results are returned
    EXPECT_EQ(result.rowCount, 0);
    EXPECT_EQ(result.rows.size(), 0);

    // Schema should still be correct (column names are prefixed with table names)
    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "person_first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "org_name");
}

TEST_F(OneHopTest, OneHopCypherQuery_PersonFilterOnly) {
    Catalog catalog;

    // Import all test CSV files
    ASSERT_NO_THROW(catalog.importNodeFromCSV(personCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(orgCsvPath));
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(worksAtCsvPath));

    // Find the tables we need
    Table* personTable = nullptr;
    Table* orgTable = nullptr;
    Table* worksAtTable = nullptr;

    for (auto& table : catalog.tables) {
        if (table.name == "person") {
            personTable = &table;
        } else if (table.name == "org") {
            orgTable = &table;
        } else if (table.name == "worksAt_fwd") {
            worksAtTable = &table;
        }
    }

    ASSERT_NE(personTable, nullptr);
    ASSERT_NE(orgTable, nullptr);
    ASSERT_NE(worksAtTable, nullptr);

    // Only filter on person predicate: age >= 35
    // This should return Diana (35) and Eve (40)
    vector<Predicate> personPredicates;
    Predicate ageFilter;
    ageFilter.column = "age";
    ageFilter.op = Predicate::Cmp::GTE;
    ageFilter.constant = int32_t(35);
    personPredicates.push_back(ageFilter);

    // Combine predicates with table names (only person predicates)
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates}
    };

    // Define projection columns with table names
    vector<pair<string, string>> projectionColumns = {
        {"person", "first_name"},
        {"org", "name"}
    };

    // Create OneHopQuery struct with table names and combined predicates
    OneHopQuery query("person", "worksAt", "org",
                      tablePredicates, projectionColumns);

    // Call the oneHop function
    auto pool = ThreadPool(1);
    Table result = oneHop(catalog, query, pool);

    // Expected: Diana (35) at StartupXYZ and Eve (40) at BigTech
    EXPECT_EQ(result.rowCount, 2);
}

TEST_F(OneHopTest, OneHopCypherQuery_Select2Tuples) {
    Catalog catalog;

    // Import all test CSV files
    ASSERT_NO_THROW(catalog.importNodeFromCSV(personCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(orgCsvPath));
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(worksAtCsvPath));

    // Find the tables we need
    Table* personTable = nullptr;
    Table* orgTable = nullptr;
    Table* worksAtTable = nullptr;

    for (auto& table : catalog.tables) {
        if (table.name == "person") {
            personTable = &table;
        } else if (table.name == "org") {
            orgTable = &table;
        } else if (table.name == "worksAt_fwd") {
            worksAtTable = &table;
        }
    }

    ASSERT_NE(personTable, nullptr);
    ASSERT_NE(orgTable, nullptr);
    ASSERT_NE(worksAtTable, nullptr);

    // Filter for people working in Toronto (DataSoft)
    // This should return Bob and Frank who both work at DataSoft in Toronto
    vector<Predicate> orgPredicates;
    Predicate cityFilter;
    cityFilter.column = "city_name";
    cityFilter.op = Predicate::Cmp::EQ;
    cityFilter.constant = int32_t(302);  // toronto = 302
    orgPredicates.push_back(cityFilter);

    // Combine predicates with table names
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"org", orgPredicates}
    };

    // Define projection columns with table names
    vector<pair<string, string>> projectionColumns = {
        {"person", "first_name"},
        {"org", "name"}
    };

    // Create OneHopQuery struct with table names and combined predicates
    OneHopQuery query("person", "worksAt", "org",
                      tablePredicates, projectionColumns);

    // Call the oneHop function
    auto pool = ThreadPool(1);
    Table result = oneHop(catalog, query, pool);

    // Expected results: Bob and Frank who both work at DataSoft in Toronto
    EXPECT_EQ(result.rowCount, 2);

    // Verify schema (column names are prefixed with table names)
    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "person_first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "org_name");

    // Check that we get Bob=12 and Frank=16 (order may vary)
    std::set<int32_t> expectedNames = {12, 16};
    std::set<int32_t> actualNames;
    for (size_t i = 0; i < result.rowCount; i++) {
        actualNames.insert(std::get<int32_t>(result.rows[i].getColumnValue("person_first_name", result.schema)));
        EXPECT_EQ(std::get<int32_t>(result.rows[i].getColumnValue("org_name", result.schema)), 202);  // DataSoft=202
    }
    EXPECT_EQ(actualNames, expectedNames);
}

TEST_F(OneHopTest, OneHopCypherQuery_Select4Tuples) {
    Catalog catalog;

    // Import all test CSV files
    ASSERT_NO_THROW(catalog.importNodeFromCSV(personCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(orgCsvPath));
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(worksAtCsvPath));

    // Find the tables we need
    Table* personTable = nullptr;
    Table* orgTable = nullptr;
    Table* worksAtTable = nullptr;

    for (auto& table : catalog.tables) {
        if (table.name == "person") {
            personTable = &table;
        } else if (table.name == "org") {
            orgTable = &table;
        } else if (table.name == "worksAt_fwd") {
            worksAtTable = &table;
        }
    }

    ASSERT_NE(personTable, nullptr);
    ASSERT_NE(orgTable, nullptr);
    ASSERT_NE(worksAtTable, nullptr);

    // Filter for people with age >= 25
    // This should return Alice (30), Charlie (28), Diana (35), and Eve (40)
    // Note: Edge predicates are not currently supported (column naming mismatch)
    vector<Predicate> personPredicates;
    Predicate ageFilter;
    ageFilter.column = "age";
    ageFilter.op = Predicate::Cmp::GTE;
    ageFilter.constant = int32_t(25);
    personPredicates.push_back(ageFilter);

    // Combine predicates with table names (no edge predicates)
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates}
    };

    // Define projection columns with table names
    vector<pair<string, string>> projectionColumns = {
        {"person", "first_name"},
        {"org", "name"}
    };

    // Create OneHopQuery struct with table names and combined predicates
    OneHopQuery query("person", "worksAt", "org",
                      tablePredicates, projectionColumns);

    // Call the oneHop function
    auto pool = ThreadPool(1);
    Table result = oneHop(catalog, query, pool);

    // Expected results: Alice (30, TechCorp), Charlie (28, InnovateInc), Diana (35, StartupXYZ), Eve (40, BigTech)
    // All are >= 25 years old
    EXPECT_EQ(result.rowCount, 4);

    // Verify schema (column names are prefixed with table names)
    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "person_first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "org_name");

    // Check that we get the expected 4 people (Alice=11, Charlie=13, Diana=14, Eve=15)
    std::set<int32_t> expectedNames = {11, 13, 14, 15};
    std::set<int32_t> actualNames;
    std::map<int32_t, int32_t> expectedCompanies = {
        {11, 201},   // Alice -> TechCorp
        {13, 203},   // Charlie -> InnovateInc
        {14, 204},   // Diana -> StartupXYZ
        {15, 205}    // Eve -> BigTech
    };

    for (size_t i = 0; i < result.rowCount; i++) {
        auto firstName = std::get<int32_t>(result.rows[i].getColumnValue("person_first_name", result.schema));
        auto companyName = std::get<int32_t>(result.rows[i].getColumnValue("org_name", result.schema));
        actualNames.insert(firstName);

        // Check if the company matches
        if (expectedCompanies.count(firstName)) {
            EXPECT_EQ(companyName, expectedCompanies[firstName]);
        }
    }
    EXPECT_EQ(actualNames, expectedNames);
}

// Test multi-table projection - returns columns from all three tables
TEST_F(OneHopTest, OneHopCypherQuery_SelectAllColumns) {
    Catalog catalog;

    ASSERT_NO_THROW(catalog.importNodeFromCSV(personCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(orgCsvPath));
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(worksAtCsvPath));

    // Filter for a specific person to get predictable results
    vector<Predicate> personPredicates;
    Predicate ageFilter;
    ageFilter.column = "age";
    ageFilter.op = Predicate::Cmp::EQ;
    ageFilter.constant = int32_t(30);  // Only Alice has age 30
    personPredicates.push_back(ageFilter);

    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates}
    };

    // Project representative columns from all three tables
    // (SELECT * would exceed 48-byte ROW_DATA_MAX_SIZE with 3 tables)
    vector<pair<string, string>> projectionColumns = {
        {"person", "first_name"},
        {"person", "age"},
        {"org", "name"},
        {"org", "city_name"},
        {"worksAt", "since"}
    };

    OneHopQuery query("person", "worksAt", "org",
                      tablePredicates, projectionColumns);

    ThreadPool pool(1);
    Table result = oneHop(catalog, query, pool);

    // Should return 1 row (Alice -> TechCorp)
    EXPECT_EQ(result.rowCount, 1);

    // Schema should include columns from all three tables
    EXPECT_EQ(result.schema.columnMetas.size(), 5);

    // Verify we can access data from different tables
    // Person data
    EXPECT_EQ(std::get<int32_t>(result.rows[0].getColumnValue("person_first_name", result.schema)), 11);  // Alice
    EXPECT_EQ(std::get<int32_t>(result.rows[0].getColumnValue("person_age", result.schema)), 30);

    // Org data
    EXPECT_EQ(std::get<int32_t>(result.rows[0].getColumnValue("org_name", result.schema)), 201);  // TechCorp
    EXPECT_EQ(std::get<int32_t>(result.rows[0].getColumnValue("org_city_name", result.schema)), 301);  // waterloo

    // Edge data
    EXPECT_EQ(std::get<int32_t>(result.rows[0].getColumnValue("since", result.schema)), 5);
}

// Test with multiple predicates on the same table (AND logic)
TEST_F(OneHopTest, OneHopCypherQuery_MultiplePredicatesSameTable) {
    Catalog catalog;

    ASSERT_NO_THROW(catalog.importNodeFromCSV(personCsvPath));
    ASSERT_NO_THROW(catalog.importNodeFromCSV(orgCsvPath));
    ASSERT_NO_THROW(catalog.importEdgeFromCSV(worksAtCsvPath));

    // Filter for people with age > 25 AND age < 35
    // This should return: Charlie (28), Alice (30)
    // Excluded: Bob (22), Frank (24), Diana (35), Eve (40)
    vector<Predicate> personPredicates;

    Predicate ageFilterGT;
    ageFilterGT.column = "age";
    ageFilterGT.op = Predicate::Cmp::GT;
    ageFilterGT.constant = int32_t(25);
    personPredicates.push_back(ageFilterGT);

    Predicate ageFilterLT;
    ageFilterLT.column = "age";
    ageFilterLT.op = Predicate::Cmp::LT;
    ageFilterLT.constant = int32_t(35);
    personPredicates.push_back(ageFilterLT);

    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates}
    };

    vector<pair<string, string>> projectionColumns = {
        {"person", "first_name"},
        {"person", "age"},
        {"org", "name"}
    };

    OneHopQuery query("person", "worksAt", "org",
                      tablePredicates, projectionColumns);

    ThreadPool pool(1);
    Table result = oneHop(catalog, query, pool);

    // Should return 2 rows: Alice (30) and Charlie (28)
    EXPECT_EQ(result.rowCount, 2);

    // Collect results (Alice=11, Charlie=13)
    std::set<int32_t> actualNames;
    for (size_t i = 0; i < result.rowCount; i++) {
        auto firstName = std::get<int32_t>(result.rows[i].getColumnValue("person_first_name", result.schema));
        auto age = std::get<int32_t>(result.rows[i].getColumnValue("person_age", result.schema));
        actualNames.insert(firstName);

        // Verify age is within range
        EXPECT_GT(age, 25);
        EXPECT_LT(age, 35);
    }

    std::set<int32_t> expectedNames = {11, 13};
    EXPECT_EQ(actualNames, expectedNames);
}
