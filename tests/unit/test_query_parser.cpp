/**
 * Test SQL query parser
 */

#include <iostream>
#include <cassert>
#include <fstream>
#include "../../src/query/query_parser.h"
#include "../../src/query/query_tokenizer.h"

void test_tokenizer_basic() {
    std::cout << "\n=== Testing Basic Tokenization ===" << std::endl;
    
    QueryTokenizer tokenizer;
    std::string query = "SELECT * FROM supplier WHERE S_NATIONKEY = 10";
    
    auto tokens = tokenizer.tokenize(query);
    
    assert(tokens[0].type == TokenType::SELECT);
    assert(tokens[1].type == TokenType::STAR);
    assert(tokens[2].type == TokenType::FROM);
    assert(tokens[3].type == TokenType::IDENTIFIER);
    assert(tokens[3].value == "supplier");
    assert(tokens[4].type == TokenType::WHERE);
    assert(tokens[5].type == TokenType::IDENTIFIER);
    assert(tokens[5].value == "S_NATIONKEY");
    assert(tokens[6].type == TokenType::EQUALS);
    assert(tokens[7].type == TokenType::NUMBER);
    assert(tokens[7].value == "10");
    
    std::cout << "  Tokens: ";
    for (const auto& tok : tokens) {
        std::cout << tok.to_string() << " ";
    }
    std::cout << std::endl;
    std::cout << "  ✓ Basic tokenization test passed" << std::endl;
}

void test_tokenizer_operators() {
    std::cout << "\n=== Testing Operator Tokenization ===" << std::endl;
    
    QueryTokenizer tokenizer;
    
    // Test various operators
    std::string query = "WHERE A >= B - 100 AND C <= D + 50 AND E > F AND G < H AND I != J";
    auto tokens = tokenizer.tokenize(query);
    
    // Find and verify operators
    bool found_gte = false, found_lte = false, found_gt = false, found_lt = false, found_ne = false;
    
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::GREATER_EQ) found_gte = true;
        if (tok.type == TokenType::LESS_EQ) found_lte = true;
        if (tok.type == TokenType::GREATER) found_gt = true;
        if (tok.type == TokenType::LESS) found_lt = true;
        if (tok.type == TokenType::NOT_EQUALS) found_ne = true;
    }
    
    assert(found_gte);
    assert(found_lte);
    assert(found_gt);
    assert(found_lt);
    assert(found_ne);
    
    std::cout << "  ✓ Operator tokenization test passed" << std::endl;
}

void test_parse_simple_query() {
    std::cout << "\n=== Testing Simple Query Parsing ===" << std::endl;
    
    QueryParser parser;
    std::string query = "SELECT * FROM supplier, nation WHERE supplier.S_NATIONKEY = nation.N_NATIONKEY";
    
    ParsedQuery result = parser.parse(query);
    
    assert(result.is_select_star());
    assert(result.num_tables() == 2);
    assert(result.tables[0] == "supplier");
    assert(result.tables[1] == "nation");
    assert(result.num_joins() == 1);
    assert(result.join_conditions[0].is_equality());
    
    std::cout << "  Parsed query:\n" << result.to_string() << std::endl;
    std::cout << "  ✓ Simple query parsing test passed" << std::endl;
}

void test_parse_tpch_tb1() {
    std::cout << "\n=== Testing TPC-H TB1 Query ===" << std::endl;
    
    QueryParser parser;
    std::string query = 
        "SELECT * "
        "FROM supplier1, supplier2 "
        "WHERE supplier2.S2_S_ACCTBAL >= supplier1.S1_S_ACCTBAL - 100 "
        "AND supplier2.S2_S_ACCTBAL <= supplier1.S1_S_ACCTBAL + 1000";
    
    ParsedQuery result = parser.parse(query);
    
    assert(result.num_tables() == 2);
    assert(result.tables[0] == "supplier1");
    assert(result.tables[1] == "supplier2");
    
    // Should have one merged band join condition
    assert(result.num_joins() == 1);
    
    const auto& join = result.join_conditions[0];
    assert(join.get_source_table() == "supplier2");
    assert(join.get_source_column() == "S2_S_ACCTBAL");
    assert(join.get_target_table() == "supplier1");
    assert(join.get_target_column() == "S1_S_ACCTBAL");
    assert(join.get_deviation1() == -100);
    assert(join.get_deviation2() == 1000);
    
    std::cout << "  Tables: " << result.tables[0] << ", " << result.tables[1] << std::endl;
    std::cout << "  Join: " << join.to_string() << std::endl;
    std::cout << "  ✓ TPC-H TB1 parsing test passed" << std::endl;
}

void test_parse_tpch_tb2() {
    std::cout << "\n=== Testing TPC-H TB2 Query ===" << std::endl;
    
    QueryParser parser;
    std::string query = 
        "SELECT * "
        "FROM part1, part2 "
        "WHERE part2.P2_P_RETAILPRICE >= part1.P1_P_RETAILPRICE - 50 "
        "AND part2.P2_P_RETAILPRICE <= part1.P1_P_RETAILPRICE + 40";
    
    ParsedQuery result = parser.parse(query);
    
    assert(result.num_tables() == 2);
    assert(result.num_joins() == 1);
    
    const auto& join = result.join_conditions[0];
    assert(join.get_deviation1() == -50);
    assert(join.get_deviation2() == 40);
    
    std::cout << "  Join: " << join.to_string() << std::endl;
    std::cout << "  ✓ TPC-H TB2 parsing test passed" << std::endl;
}

void test_parse_tpch_tm1() {
    std::cout << "\n=== Testing TPC-H TM1 Query ===" << std::endl;
    
    QueryParser parser;
    std::string query = 
        "SELECT * "
        "FROM customer, orders, lineitem "
        "WHERE customer.C_CUSTKEY = orders.O_CUSTKEY "
        "AND orders.O_ORDERKEY = lineitem.L_ORDERKEY";
    
    ParsedQuery result = parser.parse(query);
    
    assert(result.num_tables() == 3);
    assert(result.tables[0] == "customer");
    assert(result.tables[1] == "orders");
    assert(result.tables[2] == "lineitem");
    assert(result.num_joins() == 2);
    
    // Both should be equality joins
    assert(result.join_conditions[0].is_equality());
    assert(result.join_conditions[1].is_equality());
    
    std::cout << "  Tables: " << result.num_tables() << std::endl;
    std::cout << "  Joins: " << result.num_joins() << std::endl;
    for (const auto& join : result.join_conditions) {
        std::cout << "    " << join.to_string() << std::endl;
    }
    std::cout << "  ✓ TPC-H TM1 parsing test passed" << std::endl;
}

void test_parse_tpch_tm2() {
    std::cout << "\n=== Testing TPC-H TM2 Query ===" << std::endl;
    
    QueryParser parser;
    std::string query = 
        "SELECT * "
        "FROM supplier, customer, nation1, nation2 "
        "WHERE supplier.S_NATIONKEY = nation1.N1_N_NATIONKEY "
        "AND customer.C_NATIONKEY = nation2.N2_N_NATIONKEY "
        "AND nation1.N1_N_REGIONKEY = nation2.N2_N_REGIONKEY";
    
    ParsedQuery result = parser.parse(query);
    
    assert(result.num_tables() == 4);
    assert(result.num_joins() == 3);
    
    std::cout << "  Tables: " << result.num_tables() << std::endl;
    std::cout << "  Joins: " << result.num_joins() << std::endl;
    std::cout << "  ✓ TPC-H TM2 parsing test passed" << std::endl;
}

void test_parse_tpch_tm3() {
    std::cout << "\n=== Testing TPC-H TM3 Query ===" << std::endl;
    
    QueryParser parser;
    std::string query = 
        "SELECT * "
        "FROM nation, supplier, customer, orders, lineitem "
        "WHERE nation.N_NATIONKEY = supplier.S_NATIONKEY "
        "AND supplier.S_NATIONKEY = customer.C_NATIONKEY "
        "AND customer.C_CUSTKEY = orders.O_CUSTKEY "
        "AND orders.O_ORDERKEY = lineitem.L_ORDERKEY";
    
    ParsedQuery result = parser.parse(query);
    
    assert(result.num_tables() == 5);
    assert(result.num_joins() == 4);
    
    std::cout << "  Tables: " << result.num_tables() << std::endl;
    std::cout << "  Joins: " << result.num_joins() << std::endl;
    std::cout << "  ✓ TPC-H TM3 parsing test passed" << std::endl;
}

void test_parse_actual_query_files() {
    std::cout << "\n=== Testing Actual Query Files ===" << std::endl;
    
    QueryParser parser;
    std::vector<std::string> query_files = {
        "../../../queries/tpch_tb1.sql",
        "../../../queries/tpch_tb2.sql",
        "../../../queries/tpch_tm1.sql",
        "../../../queries/tpch_tm2.sql",
        "../../../queries/tpch_tm3.sql"
    };
    
    for (const auto& file : query_files) {
        std::ifstream ifs(file);
        if (!ifs.is_open()) {
            std::cout << "  Warning: Could not open " << file << std::endl;
            continue;
        }
        
        std::string query((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
        
        try {
            ParsedQuery result = parser.parse(query);
            std::cout << "  " << file << ": " 
                     << result.num_tables() << " tables, "
                     << result.num_joins() << " joins" << std::endl;
        } catch (const ParseException& e) {
            std::cout << "  Error parsing " << file << ": " << e.what() << std::endl;
            assert(false);
        }
    }
    
    std::cout << "  ✓ Actual query files test passed" << std::endl;
}

void test_condition_merging() {
    std::cout << "\n=== Testing Condition Merging ===" << std::endl;
    
    QueryParser parser;
    
    // Test query with multiple conditions on same columns
    std::string query = 
        "SELECT * FROM A, B "
        "WHERE A.x >= B.y - 50 "
        "AND A.x <= B.y + 100 "
        "AND A.z = B.w";
    
    ParsedQuery result = parser.parse(query);
    
    // Should have 2 join conditions: one merged band join and one equality
    assert(result.num_joins() == 2);
    
    // Find the band join
    bool found_band = false;
    bool found_equality = false;
    
    for (const auto& join : result.join_conditions) {
        if (join.get_source_column() == "x") {
            assert(join.get_deviation1() == -50);
            assert(join.get_deviation2() == 100);
            found_band = true;
        }
        if (join.get_source_column() == "z") {
            assert(join.is_equality());
            found_equality = true;
        }
    }
    
    assert(found_band);
    assert(found_equality);
    
    std::cout << "  ✓ Condition merging test passed" << std::endl;
}

int main() {
    std::cout << "Query Parser Unit Tests" << std::endl;
    std::cout << "=======================" << std::endl;
    
    try {
        // Test tokenizer
        test_tokenizer_basic();
        test_tokenizer_operators();
        
        // Test parser
        test_parse_simple_query();
        test_parse_tpch_tb1();
        test_parse_tpch_tb2();
        test_parse_tpch_tm1();
        test_parse_tpch_tm2();
        test_parse_tpch_tm3();
        test_condition_merging();
        
        // Test with actual files if available
        test_parse_actual_query_files();
        
        std::cout << "\n=== All query parser tests passed! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}