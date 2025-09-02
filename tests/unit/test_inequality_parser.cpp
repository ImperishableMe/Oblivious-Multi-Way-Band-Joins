/**
 * Test inequality parser for SQL conditions
 */

#include <iostream>
#include <cassert>
#include "../app/utils/inequality_parser.h"
#include "../app/utils/condition_merger.h"

void test_equality_parsing() {
    std::cout << "\n=== Testing Equality Condition Parsing ===" << std::endl;
    
    std::string condition = "supplier.S_NATIONKEY = nation.N_NATIONKEY";
    auto result = InequalityParser::parse(condition);
    
    assert(result.has_value());
    JoinConstraint constraint = result.value();
    
    assert(constraint.get_source_table() == "supplier");
    assert(constraint.get_source_column() == "S_NATIONKEY");
    assert(constraint.get_target_table() == "nation");
    assert(constraint.get_target_column() == "N_NATIONKEY");
    assert(constraint.is_equality());
    
    std::cout << "  Input: " << condition << std::endl;
    std::cout << "  Parsed: " << constraint.to_string() << std::endl;
    std::cout << "  ✓ Equality parsing test passed" << std::endl;
}

void test_greater_equal_parsing() {
    std::cout << "\n=== Testing >= Condition Parsing ===" << std::endl;
    
    // Test without deviation
    std::string condition1 = "supplier2.S2_S_ACCTBAL >= supplier1.S1_S_ACCTBAL";
    auto result1 = InequalityParser::parse(condition1);
    assert(result1.has_value());
    
    JoinConstraint c1 = result1.value();
    assert(c1.get_deviation1() == 0);
    assert(c1.get_equality1() == EQ);
    assert(c1.get_deviation2() == JOIN_ATTR_POS_INF);
    
    std::cout << "  Input: " << condition1 << std::endl;
    std::cout << "  Parsed: " << c1.to_string() << std::endl;
    
    // Test with negative deviation
    std::string condition2 = "supplier2.S2_S_ACCTBAL >= supplier1.S1_S_ACCTBAL - 100";
    auto result2 = InequalityParser::parse(condition2);
    assert(result2.has_value());
    
    JoinConstraint c2 = result2.value();
    assert(c2.get_deviation1() == -100);
    assert(c2.get_equality1() == EQ);
    assert(c2.get_deviation2() == JOIN_ATTR_POS_INF);
    
    std::cout << "  Input: " << condition2 << std::endl;
    std::cout << "  Parsed: " << c2.to_string() << std::endl;
    std::cout << "  ✓ >= parsing test passed" << std::endl;
}

void test_less_equal_parsing() {
    std::cout << "\n=== Testing <= Condition Parsing ===" << std::endl;
    
    // Test with positive deviation
    std::string condition = "supplier2.S2_S_ACCTBAL <= supplier1.S1_S_ACCTBAL + 1000";
    auto result = InequalityParser::parse(condition);
    assert(result.has_value());
    
    JoinConstraint constraint = result.value();
    assert(constraint.get_deviation1() == JOIN_ATTR_NEG_INF);
    assert(constraint.get_deviation2() == 1000);
    assert(constraint.get_equality2() == EQ);
    
    std::cout << "  Input: " << condition << std::endl;
    std::cout << "  Parsed: " << constraint.to_string() << std::endl;
    std::cout << "  ✓ <= parsing test passed" << std::endl;
}

void test_greater_than_parsing() {
    std::cout << "\n=== Testing > Condition Parsing ===" << std::endl;
    
    std::string condition = "A.x > B.y";
    auto result = InequalityParser::parse(condition);
    assert(result.has_value());
    
    JoinConstraint constraint = result.value();
    assert(constraint.get_deviation1() == 0);
    assert(constraint.get_equality1() == NEQ);  // Open lower bound
    assert(constraint.get_deviation2() == JOIN_ATTR_POS_INF);
    
    std::cout << "  Input: " << condition << std::endl;
    std::cout << "  Parsed: " << constraint.to_string() << std::endl;
    std::cout << "  ✓ > parsing test passed" << std::endl;
}

void test_less_than_parsing() {
    std::cout << "\n=== Testing < Condition Parsing ===" << std::endl;
    
    std::string condition = "A.x < B.y + 10";
    auto result = InequalityParser::parse(condition);
    assert(result.has_value());
    
    JoinConstraint constraint = result.value();
    assert(constraint.get_deviation1() == JOIN_ATTR_NEG_INF);
    assert(constraint.get_deviation2() == 10);
    assert(constraint.get_equality2() == NEQ);  // Open upper bound
    
    std::cout << "  Input: " << condition << std::endl;
    std::cout << "  Parsed: " << constraint.to_string() << std::endl;
    std::cout << "  ✓ < parsing test passed" << std::endl;
}

void test_qualified_name_parsing() {
    std::cout << "\n=== Testing Qualified Name Parsing ===" << std::endl;
    
    auto [table1, col1] = InequalityParser::parse_qualified_name("supplier.S_NATIONKEY");
    assert(table1 == "supplier");
    assert(col1 == "S_NATIONKEY");
    
    auto [table2, col2] = InequalityParser::parse_qualified_name("  nation.N_NATIONKEY  ");
    assert(table2 == "nation");
    assert(col2 == "N_NATIONKEY");
    
    std::cout << "  supplier.S_NATIONKEY → [" << table1 << ", " << col1 << "]" << std::endl;
    std::cout << "  nation.N_NATIONKEY → [" << table2 << ", " << col2 << "]" << std::endl;
    std::cout << "  ✓ Qualified name parsing test passed" << std::endl;
}

void test_deviation_parsing() {
    std::cout << "\n=== Testing Deviation Parsing ===" << std::endl;
    
    assert(InequalityParser::parse_deviation("B.y") == 0);
    assert(InequalityParser::parse_deviation("B.y + 100") == 100);
    assert(InequalityParser::parse_deviation("B.y - 50") == -50);
    assert(InequalityParser::parse_deviation("B.y + 1000") == 1000);
    assert(InequalityParser::parse_deviation("B.y-25") == -25);  // No spaces
    
    std::cout << "  B.y → " << InequalityParser::parse_deviation("B.y") << std::endl;
    std::cout << "  B.y + 100 → " << InequalityParser::parse_deviation("B.y + 100") << std::endl;
    std::cout << "  B.y - 50 → " << InequalityParser::parse_deviation("B.y - 50") << std::endl;
    std::cout << "  ✓ Deviation parsing test passed" << std::endl;
}

void test_tpch_tb1_conditions() {
    std::cout << "\n=== Testing TPC-H TB1 Query Conditions ===" << std::endl;
    
    // Parse the two conditions from tpch_tb1
    std::string cond1 = "supplier2.S2_S_ACCTBAL >= supplier1.S1_S_ACCTBAL - 100";
    std::string cond2 = "supplier2.S2_S_ACCTBAL <= supplier1.S1_S_ACCTBAL + 1000";
    
    auto parsed1 = InequalityParser::parse(cond1);
    auto parsed2 = InequalityParser::parse(cond2);
    
    assert(parsed1.has_value());
    assert(parsed2.has_value());
    
    std::cout << "  Condition 1: " << cond1 << std::endl;
    std::cout << "  Parsed 1: " << parsed1.value().to_string() << std::endl;
    
    std::cout << "  Condition 2: " << cond2 << std::endl;
    std::cout << "  Parsed 2: " << parsed2.value().to_string() << std::endl;
    
    // Merge the conditions
    auto merged = ConditionMerger::merge(parsed1.value(), parsed2.value());
    assert(merged.has_value());
    
    JoinConstraint final_constraint = merged.value();
    assert(final_constraint.get_deviation1() == -100);
    assert(final_constraint.get_deviation2() == 1000);
    
    std::cout << "  Merged: " << final_constraint.to_string() << std::endl;
    std::cout << "  ✓ TPC-H TB1 conditions test passed" << std::endl;
}

void test_tpch_tb2_conditions() {
    std::cout << "\n=== Testing TPC-H TB2 Query Conditions ===" << std::endl;
    
    // Parse the conditions from tpch_tb2
    std::string cond1 = "part2.P2_P_RETAILPRICE >= part1.P1_P_RETAILPRICE - 50";
    std::string cond2 = "part2.P2_P_RETAILPRICE <= part1.P1_P_RETAILPRICE + 40";
    
    auto parsed1 = InequalityParser::parse(cond1);
    auto parsed2 = InequalityParser::parse(cond2);
    
    assert(parsed1.has_value());
    assert(parsed2.has_value());
    
    std::cout << "  Condition 1: " << cond1 << std::endl;
    std::cout << "  Parsed 1: " << parsed1.value().to_string() << std::endl;
    
    std::cout << "  Condition 2: " << cond2 << std::endl;
    std::cout << "  Parsed 2: " << parsed2.value().to_string() << std::endl;
    
    // Merge the conditions
    auto merged = ConditionMerger::merge(parsed1.value(), parsed2.value());
    assert(merged.has_value());
    
    JoinConstraint final_constraint = merged.value();
    assert(final_constraint.get_deviation1() == -50);
    assert(final_constraint.get_deviation2() == 40);
    
    std::cout << "  Merged: " << final_constraint.to_string() << std::endl;
    std::cout << "  ✓ TPC-H TB2 conditions test passed" << std::endl;
}

void test_non_join_conditions() {
    std::cout << "\n=== Testing Non-Join Condition Detection ===" << std::endl;
    
    // These are filter conditions, not join conditions
    assert(!InequalityParser::is_join_condition("S_ACCTBAL >= 1000"));
    assert(!InequalityParser::is_join_condition("price > 50.0"));
    assert(!InequalityParser::is_join_condition("quantity = 10"));
    
    // These are join conditions
    assert(InequalityParser::is_join_condition("A.x = B.y"));
    assert(InequalityParser::is_join_condition("table1.col1 >= table2.col2"));
    
    auto result1 = InequalityParser::parse("S_ACCTBAL >= 1000");
    assert(!result1.has_value());
    
    auto result2 = InequalityParser::parse("A.x = B.y");
    assert(result2.has_value());
    
    std::cout << "  ✓ Non-join condition detection test passed" << std::endl;
}

void test_whitespace_handling() {
    std::cout << "\n=== Testing Whitespace Handling ===" << std::endl;
    
    // Test with various whitespace
    std::string cond1 = "  supplier.S_NATIONKEY   =   nation.N_NATIONKEY  ";
    std::string cond2 = "A.x>=B.y-100";  // No spaces
    std::string cond3 = "  A.x  <  B.y  +  50  ";  // Lots of spaces
    
    auto r1 = InequalityParser::parse(cond1);
    auto r2 = InequalityParser::parse(cond2);
    auto r3 = InequalityParser::parse(cond3);
    
    assert(r1.has_value());
    assert(r2.has_value());
    assert(r3.has_value());
    
    assert(r1.value().is_equality());
    assert(r2.value().get_deviation1() == -100);
    assert(r3.value().get_deviation2() == 50);
    
    std::cout << "  With spaces: " << cond1 << std::endl;
    std::cout << "  No spaces: " << cond2 << std::endl;
    std::cout << "  Many spaces: " << cond3 << std::endl;
    std::cout << "  ✓ Whitespace handling test passed" << std::endl;
}

int main() {
    std::cout << "Inequality Parser Unit Tests" << std::endl;
    std::cout << "============================" << std::endl;
    
    try {
        test_qualified_name_parsing();
        test_deviation_parsing();
        test_equality_parsing();
        test_greater_equal_parsing();
        test_less_equal_parsing();
        test_greater_than_parsing();
        test_less_than_parsing();
        test_tpch_tb1_conditions();
        test_tpch_tb2_conditions();
        test_non_join_conditions();
        test_whitespace_handling();
        
        std::cout << "\n=== All parser tests passed! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}