/**
 * Unit tests for JoinConstraint and join condition bounds
 * 
 * Tests:
 * 1. Equality join conditions
 * 2. Band join conditions
 * 3. Reverse constraint operations
 * 4. Boundary entry generation
 * 5. Constraint validation
 */

#include <iostream>
#include <cassert>
#include "../../app/join/join_constraint.h"
#include "../../app/data_structures/data_types.h"

void test_equality_join() {
    std::cout << "\n=== Testing Equality Join ===" << std::endl;
    
    // Create equality join: supplier1.S1_SUPPKEY = supplier2.S2_SUPPKEY
    JoinConstraint eq_join = JoinConstraint::equality(
        "supplier1", "S1_SUPPKEY",
        "supplier2", "S2_SUPPKEY"
    );
    
    // Check parameters
    assert(eq_join.get_deviation1() == 0);
    assert(eq_join.get_deviation2() == 0);
    assert(eq_join.get_equality1() == EQ);
    assert(eq_join.get_equality2() == EQ);
    assert(eq_join.is_equality());
    assert(eq_join.is_valid());
    
    std::cout << "  Forward: " << eq_join.to_string() << std::endl;
    
    // Test reverse
    JoinConstraint reversed = eq_join.reverse();
    assert(reversed.get_source_table() == "supplier2");
    assert(reversed.get_target_table() == "supplier1");
    assert(reversed.get_deviation1() == 0);
    assert(reversed.get_deviation2() == 0);
    assert(reversed.is_equality());
    
    std::cout << "  Reverse: " << reversed.to_string() << std::endl;
    std::cout << "  ✓ Equality join test passed" << std::endl;
}

void test_band_join() {
    std::cout << "\n=== Testing Band Join ===" << std::endl;
    
    // Create band join from tpch_tb1.sql:
    // supplier2.S2_S_ACCTBAL >= supplier1.S1_S_ACCTBAL - 100
    // supplier2.S2_S_ACCTBAL <= supplier1.S1_S_ACCTBAL + 1000
    // This means: S2_S_ACCTBAL IN [S1_S_ACCTBAL - 100, S1_S_ACCTBAL + 1000]
    JoinConstraint band_join = JoinConstraint::band(
        "supplier2", "S2_S_ACCTBAL",
        "supplier1", "S1_S_ACCTBAL",
        -100,   // lower bound: target - 100
        1000,   // upper bound: target + 1000
        true,   // lower inclusive (>=)
        true    // upper inclusive (<=)
    );
    
    // Check parameters
    assert(band_join.get_deviation1() == -100);
    assert(band_join.get_deviation2() == 1000);
    assert(band_join.get_equality1() == EQ);  // closed interval
    assert(band_join.get_equality2() == EQ);  // closed interval
    assert(!band_join.is_equality());
    assert(band_join.is_valid());
    
    std::cout << "  Forward: " << band_join.to_string() << std::endl;
    
    // Test reverse: Should give us S1_S_ACCTBAL IN [S2_S_ACCTBAL - 1000, S2_S_ACCTBAL + 100]
    JoinConstraint reversed = band_join.reverse();
    assert(reversed.get_source_table() == "supplier1");
    assert(reversed.get_target_table() == "supplier2");
    assert(reversed.get_deviation1() == -1000);  // negated upper becomes lower
    assert(reversed.get_deviation2() == 100);    // negated lower becomes upper
    assert(reversed.get_equality1() == EQ);
    assert(reversed.get_equality2() == EQ);
    
    std::cout << "  Reverse: " << reversed.to_string() << std::endl;
    std::cout << "  ✓ Band join test passed" << std::endl;
}

void test_open_interval() {
    std::cout << "\n=== Testing Open Interval ===" << std::endl;
    
    // Create open interval: A.val > B.val (not >=)
    // This means A.val IN (B.val, +∞)
    JoinConstraint open_join(
        "A", "val",
        "B", "val",
        0, NEQ,     // lower bound: B.val + 0, open
        JOIN_ATTR_POS_INF, EQ  // upper bound: +∞
    );
    
    assert(open_join.get_deviation1() == 0);
    assert(open_join.get_equality1() == NEQ);  // open lower bound
    assert(!open_join.is_equality());
    
    std::cout << "  Forward: " << open_join.to_string() << std::endl;
    
    // Reverse should give: B.val IN (-∞, A.val)
    JoinConstraint reversed = open_join.reverse();
    assert(reversed.get_deviation1() < 0);  // Should be negative infinity
    assert(reversed.get_deviation2() == 0);
    assert(reversed.get_equality2() == NEQ);  // open upper bound
    
    std::cout << "  Reverse: " << reversed.to_string() << std::endl;
    std::cout << "  ✓ Open interval test passed" << std::endl;
}

void test_constraint_params() {
    std::cout << "\n=== Testing Constraint Parameters ===" << std::endl;
    
    JoinConstraint constraint = JoinConstraint::band(
        "orders", "O_CUSTKEY",
        "customer", "C_CUSTKEY",
        -10, 20,
        true, false  // [lower, upper)
    );
    
    ConstraintParam params = constraint.get_params();
    assert(params.deviation1 == -10);
    assert(params.deviation2 == 20);
    assert(params.equality1 == EQ);   // closed lower
    assert(params.equality2 == NEQ);  // open upper
    
    std::cout << "  Params: dev1=" << params.deviation1 
              << ", eq1=" << (params.equality1 == EQ ? "EQ" : "NEQ")
              << ", dev2=" << params.deviation2
              << ", eq2=" << (params.equality2 == EQ ? "EQ" : "NEQ") << std::endl;
    std::cout << "  ✓ Constraint parameters test passed" << std::endl;
}

void test_invalid_constraints() {
    std::cout << "\n=== Testing Invalid Constraints ===" << std::endl;
    
    // Invalid: lower > upper
    JoinConstraint invalid1(
        "A", "x", "B", "y",
        100, EQ,   // lower: B.y + 100
        50, EQ     // upper: B.y + 50
    );
    assert(!invalid1.is_valid());
    std::cout << "  ✓ Detected invalid constraint (lower > upper)" << std::endl;
    
    // Valid: lower == upper with both EQ (equality join)
    JoinConstraint valid_eq(
        "A", "x", "B", "y",
        5, EQ,
        5, EQ
    );
    assert(valid_eq.is_valid());
    std::cout << "  ✓ Valid constraint (lower == upper, both EQ)" << std::endl;
    
    // Edge case: empty interval (lower == upper but one is NEQ)
    JoinConstraint empty(
        "A", "x", "B", "y",
        10, NEQ,   // (B.y + 10
        10, EQ     // B.y + 10]
    );
    // This creates an empty interval - implementation specific handling
    std::cout << "  ✓ Constraint validation tests passed" << std::endl;
}

void test_boundary_entries_equality() {
    std::cout << "\n=== Testing Boundary Entries (Equality) ===" << std::endl;
    
    // Create a target entry
    Entry target;
    target.join_attr = 100;
    target.field_type = TARGET;
    target.original_index = 5;
    
    // Create equality join condition
    JoinCondition eq_condition = JoinCondition::equality(
        "parent", "child",
        "P_KEY", "C_KEY"
    );
    
    // Generate boundary entries
    auto [start_entry, end_entry] = eq_condition.create_boundary_entries(target);
    
    // For equality join, both boundaries should be at the same value
    assert(start_entry.join_attr == 100);  // 100 + 0
    assert(end_entry.join_attr == 100);    // 100 + 0
    assert(start_entry.field_type == START);
    assert(end_entry.field_type == END);
    assert(start_entry.equality_type == EQ);
    assert(end_entry.equality_type == EQ);
    
    std::cout << "  Target join_attr: " << target.join_attr << std::endl;
    std::cout << "  START entry: join_attr=" << start_entry.join_attr 
              << ", equality=" << (start_entry.equality_type == EQ ? "EQ" : "NEQ") << std::endl;
    std::cout << "  END entry: join_attr=" << end_entry.join_attr
              << ", equality=" << (end_entry.equality_type == EQ ? "EQ" : "NEQ") << std::endl;
    std::cout << "  ✓ Equality boundary entries test passed" << std::endl;
}

void test_boundary_entries_band() {
    std::cout << "\n=== Testing Boundary Entries (Band) ===" << std::endl;
    
    // Create a target entry
    Entry target;
    target.join_attr = 500;
    target.field_type = TARGET;
    target.original_index = 10;
    
    // Create band join condition: [target - 100, target + 1000]
    JoinCondition band_condition = JoinCondition::band(
        "parent", "child",
        "P_VAL", "C_VAL",
        -100, 1000,
        true, true  // both inclusive
    );
    
    // Generate boundary entries
    auto [start_entry, end_entry] = band_condition.create_boundary_entries(target);
    
    // Check boundaries are correctly offset
    assert(start_entry.join_attr == 400);   // 500 - 100
    assert(end_entry.join_attr == 1500);    // 500 + 1000
    assert(start_entry.field_type == START);
    assert(end_entry.field_type == END);
    assert(start_entry.equality_type == EQ);  // closed interval
    assert(end_entry.equality_type == EQ);    // closed interval
    
    std::cout << "  Target join_attr: " << target.join_attr << std::endl;
    std::cout << "  Band: [target - 100, target + 1000]" << std::endl;
    std::cout << "  START entry: join_attr=" << start_entry.join_attr << " (expected 400)" << std::endl;
    std::cout << "  END entry: join_attr=" << end_entry.join_attr << " (expected 1500)" << std::endl;
    std::cout << "  ✓ Band boundary entries test passed" << std::endl;
}

void test_multi_way_join_scenario() {
    std::cout << "\n=== Testing Multi-way Join Scenario ===" << std::endl;
    std::cout << "  Simulating: customer → orders → lineitem" << std::endl;
    
    // First join: customer.C_CUSTKEY = orders.O_CUSTKEY
    JoinConstraint customer_orders = JoinConstraint::equality(
        "orders", "O_CUSTKEY",
        "customer", "C_CUSTKEY"
    );
    
    // Second join: orders.O_ORDERKEY = lineitem.L_ORDERKEY
    JoinConstraint orders_lineitem = JoinConstraint::equality(
        "lineitem", "L_ORDERKEY",
        "orders", "O_ORDERKEY"
    );
    
    std::cout << "  Join 1: " << customer_orders.to_string() << std::endl;
    std::cout << "  Join 2: " << orders_lineitem.to_string() << std::endl;
    
    // Test reversal for bottom-up processing
    JoinConstraint orders_customer = customer_orders.reverse();
    JoinConstraint lineitem_orders = orders_lineitem.reverse();
    
    std::cout << "  Reversed Join 1: " << orders_customer.to_string() << std::endl;
    std::cout << "  Reversed Join 2: " << lineitem_orders.to_string() << std::endl;
    
    // Verify consistency
    assert(orders_customer.get_source_table() == "customer");
    assert(orders_customer.get_target_table() == "orders");
    assert(lineitem_orders.get_source_table() == "orders");
    assert(lineitem_orders.get_target_table() == "lineitem");
    
    std::cout << "  ✓ Multi-way join scenario test passed" << std::endl;
}

int main() {
    std::cout << "Join Constraint Unit Tests" << std::endl;
    std::cout << "==========================" << std::endl;
    
    try {
        test_equality_join();
        test_band_join();
        test_open_interval();
        test_constraint_params();
        test_invalid_constraints();
        test_boundary_entries_equality();
        test_boundary_entries_band();
        test_multi_way_join_scenario();
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}