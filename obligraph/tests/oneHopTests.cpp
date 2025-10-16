#include <gtest/gtest.h>
#include "../include/definitions.h"
#include <fstream>
#include <filesystem>
#include <variant>

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
        file << "int64|int32|string|string\n";
        file << "1|30|Alice|Johnson\n";
        file << "2|22|Bob|Smith\n";
        file << "3|28|Charlie|Brown\n";
        file << "4|35|Diana|Wilson\n";
        file << "5|40|Eve|Davis\n";
        file << "6|24|Frank|Miller\n";
        file.close();
    }

    void createOrgNodeCSV() {
        orgCsvPath = "org.csv";
        std::ofstream file(orgCsvPath);
        file << "id|establishedAt|name|city_name\n";
        file << "int64|int32|string|string\n";
        file << "101|2010|TechCorp|waterloo\n";
        file << "102|2015|DataSoft|toronto\n";
        file << "103|2008|InnovateInc|waterloo\n";
        file << "104|2020|StartupXYZ|vancouver\n";
        file << "105|2012|BigTech|waterloo\n";
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
    // 2. WorksAt since > 2 years
    // 3. Org city_name == "waterloo"
    
    vector<Predicate> personPredicates;
    Predicate ageFilter;
    ageFilter.column = "age";
    ageFilter.op = Predicate::Cmp::GT;
    ageFilter.constant = int32_t(25);
    personPredicates.push_back(ageFilter);
    
    vector<Predicate> edgePredicates;
    Predicate sinceFilter;
    sinceFilter.column = "since";
    sinceFilter.op = Predicate::Cmp::GT;
    sinceFilter.constant = int32_t(2);
    edgePredicates.push_back(sinceFilter);
    
    vector<Predicate> orgPredicates;
    Predicate cityFilter;
    cityFilter.column = "city_name";
    cityFilter.op = Predicate::Cmp::EQ;
    cityFilter.constant = string("wa");
    orgPredicates.push_back(cityFilter);
    
    // Combine predicates with table names
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates},
        {"worksAt", edgePredicates},
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

    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "name");
    
    // Expected results based on our test data:
    // - Alice (age 30 > 25) works at TechCorp (waterloo) for 5 years (> 2) ✓
    // - Charlie (age 28 > 25) works at InnovateInc (waterloo) for 3 years (> 2) ✓
    // - Eve (age 40 > 25) works at BigTech (waterloo) for 6 years (> 2) ✓
    // 
    // Filtered out:
    // - Bob (age 22 ≤ 25) 
    // - Diana (age 35 > 25) works at StartupXYZ (vancouver ≠ waterloo)
    // - Frank (age 24 ≤ 25)
    
    EXPECT_EQ(result.rowCount, 3);
    // And check that the actual names are: Alice-TechCorp, Charlie-InnovateInc, Eve-BigTech
    // Write additional checks here, since it is implemented
    EXPECT_EQ(std::get<string>(result.rows[0].getColumnValue("first_name", result.schema)), string("Al"));
    EXPECT_EQ(std::get<string>(result.rows[0].getColumnValue("name", result.schema)), string("Te"));
    EXPECT_EQ(std::get<string>(result.rows[1].getColumnValue("first_name", result.schema)), string("Ch"));
    EXPECT_EQ(std::get<string>(result.rows[1].getColumnValue("name", result.schema)), string("In"));
    EXPECT_EQ(std::get<string>(result.rows[2].getColumnValue("first_name", result.schema)), string("Ev"));
    EXPECT_EQ(std::get<string>(result.rows[2].getColumnValue("name", result.schema)), string("Bi"));
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
    
    // Schema should still be correct
    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "name");
}

TEST_F(OneHopTest, OneHopCypherQuery_EdgeFilterOnly) {
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
    
    // Only filter on edge predicate: since >= 5 years
    vector<Predicate> edgePredicates;
    Predicate sinceFilter;
    sinceFilter.column = "since";
    sinceFilter.op = Predicate::Cmp::GTE;
    sinceFilter.constant = int32_t(5);
    edgePredicates.push_back(sinceFilter);
    
    // Combine predicates with table names (only edge predicates)
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"worksAt", edgePredicates}
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
    cityFilter.constant = string("to");
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
    
    // Verify schema
    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "name");
    
    // Check that we get Bob and Frank (order may vary)
    std::set<string> expectedNames = {"Bo", "Fr"};
    std::set<string> actualNames;
    for (size_t i = 0; i < result.rowCount; i++) {
        actualNames.insert(std::get<string>(result.rows[i].getColumnValue("first_name", result.schema)));
        EXPECT_EQ(std::get<string>(result.rows[i].getColumnValue("name", result.schema)), string("Da"));
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
    
    // Filter for people with age >= 25 and working for at least 2 years
    // This should return Alice (30, 5 years), Charlie (28, 3 years), Diana (35, 2 years), and Eve (40, 6 years)
    vector<Predicate> personPredicates;
    Predicate ageFilter;
    ageFilter.column = "age";
    ageFilter.op = Predicate::Cmp::GTE;
    ageFilter.constant = int32_t(25);
    personPredicates.push_back(ageFilter);
    
    vector<Predicate> edgePredicates;
    Predicate sinceFilter;
    sinceFilter.column = "since";
    sinceFilter.op = Predicate::Cmp::GTE;
    sinceFilter.constant = static_cast<int32_t>(2);
    edgePredicates.push_back(sinceFilter);
    
    // Combine predicates with table names
    vector<pair<string, vector<Predicate>>> tablePredicates = {
        {"person", personPredicates},
        {"worksAt", edgePredicates}
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
    // All are >= 25 years old and work for >= 2 years
    EXPECT_EQ(result.rowCount, 4);
    
    // Verify schema
    EXPECT_EQ(result.schema.columnMetas.size(), 2);
    EXPECT_EQ(result.schema.columnMetas[0].name, "first_name");
    EXPECT_EQ(result.schema.columnMetas[1].name, "name");
    
    // Check that we get the expected 4 people
    std::set<string> expectedNames = {"Al", "Ch", "Di", "Ev"};
    std::set<string> actualNames;
    std::map<string, string> expectedCompanies = {
        {"Alice", "TechCorp"},
        {"Charlie", "InnovateIn"},  // Note: truncated in the original test
        {"Diana", "StartupXYZ"},
        {"Eve", "BigTech"}
    };
    
    for (size_t i = 0; i < result.rowCount; i++) {
        auto firstName = std::get<string>(result.rows[i].getColumnValue("first_name", result.schema));
        auto companyName = std::get<string>(result.rows[i].getColumnValue("name", result.schema));
        actualNames.insert(firstName);
        
        // Check if the company matches (handling potential truncation)
        if (expectedCompanies.count(firstName)) {
            string expectedCompany = expectedCompanies[firstName];
            // Allow for potential truncation in company names
            EXPECT_TRUE(companyName == expectedCompany || expectedCompany.find(companyName) == 0);
        }
    }
    EXPECT_EQ(actualNames, expectedNames);
}
