/**
 * Test condition merger for join constraint intersection
 */

#include <iostream>
#include <cassert>
#include "../../src/query/condition_merger.h"
#include "../../src/core/join_constraint.h"

void test_basic_band_merge() {
    std::cout << "\n=== Testing Basic Band Merge (tpch_tb1 style) ===" << std::endl;
    
    // Create two conditions from tpch_tb1:
    // supplier2.S2_S_ACCTBAL >= supplier1.S1_S_ACCTBAL - 100
    // This translates to: S2_S_ACCTBAL IN [S1_S_ACCTBAL-100, +∞]
    JoinConstraint c1(
        "supplier2", "S2_S_ACCTBAL",
        "supplier1", "S1_S_ACCTBAL",
        -100, EQ,                    // lower bound
        JOIN_ATTR_POS_INF, EQ        // upper bound (+∞)
    );
    
    // supplier2.S2_S_ACCTBAL <= supplier1.S1_S_ACCTBAL + 1000
    // This translates to: S2_S_ACCTBAL IN [-∞, S1_S_ACCTBAL+1000]
    JoinConstraint c2(
        "supplier2", "S2_S_ACCTBAL",
        "supplier1", "S1_S_ACCTBAL",
        JOIN_ATTR_NEG_INF, EQ,       // lower bound (-∞)
        1000, EQ                     // upper bound
    );
    
    std::cout << "  C1: " << c1.to_string() << std::endl;
    std::cout << "  C2: " << c2.to_string() << std::endl;
    
    // Merge should give [S1_S_ACCTBAL-100, S1_S_ACCTBAL+1000]
    auto merged = ConditionMerger::merge(c1, c2);
    assert(merged.has_value());
    
    JoinConstraint result = merged.value();
    assert(result.get_deviation1() == -100);
    assert(result.get_deviation2() == 1000);
    assert(result.get_equality1() == EQ);
    assert(result.get_equality2() == EQ);
    
    std::cout << "  Merged: " << result.to_string() << std::endl;
    std::cout << "  ✓ Band merge test passed" << std::endl;
}

void test_overlapping_ranges() {
    std::cout << "\n=== Testing Overlapping Ranges ===" << std::endl;
    
    // C1: x IN [B-50, B+100]
    JoinConstraint c1(
        "A", "x", "B", "y",
        -50, EQ, 100, EQ
    );
    
    // C2: x IN [B-20, B+80]
    JoinConstraint c2(
        "A", "x", "B", "y",
        -20, EQ, 80, EQ
    );
    
    std::cout << "  C1: " << c1.to_string() << std::endl;
    std::cout << "  C2: " << c2.to_string() << std::endl;
    
    // Intersection should be [B-20, B+80]
    auto merged = ConditionMerger::merge(c1, c2);
    assert(merged.has_value());
    
    JoinConstraint result = merged.value();
    assert(result.get_deviation1() == -20);  // max(-50, -20) = -20
    assert(result.get_deviation2() == 80);   // min(100, 80) = 80
    
    std::cout << "  Merged: " << result.to_string() << std::endl;
    std::cout << "  ✓ Overlapping ranges test passed" << std::endl;
}

void test_no_overlap() {
    std::cout << "\n=== Testing Non-overlapping Ranges ===" << std::endl;
    
    // C1: x IN [B+100, B+200]
    JoinConstraint c1(
        "A", "x", "B", "y",
        100, EQ, 200, EQ
    );
    
    // C2: x IN [B-200, B-100]
    JoinConstraint c2(
        "A", "x", "B", "y",
        -200, EQ, -100, EQ
    );
    
    std::cout << "  C1: " << c1.to_string() << std::endl;
    std::cout << "  C2: " << c2.to_string() << std::endl;
    
    // No intersection - should return empty
    auto merged = ConditionMerger::merge(c1, c2);
    assert(!merged.has_value());
    
    std::cout << "  Merged: (empty - no overlap)" << std::endl;
    std::cout << "  ✓ Non-overlapping test passed" << std::endl;
}

void test_open_closed_intervals() {
    std::cout << "\n=== Testing Open/Closed Interval Merge ===" << std::endl;
    
    // C1: x IN (B+0, B+100]  (open lower bound)
    JoinConstraint c1(
        "A", "x", "B", "y",
        0, NEQ, 100, EQ
    );
    
    // C2: x IN [B-10, B+50)  (open upper bound)
    JoinConstraint c2(
        "A", "x", "B", "y",
        -10, EQ, 50, NEQ
    );
    
    std::cout << "  C1: " << c1.to_string() << std::endl;
    std::cout << "  C2: " << c2.to_string() << std::endl;
    
    // Merged should be (B+0, B+50) - both bounds become stricter
    auto merged = ConditionMerger::merge(c1, c2);
    assert(merged.has_value());
    
    JoinConstraint result = merged.value();
    assert(result.get_deviation1() == 0);    // max(-10, 0) = 0
    assert(result.get_equality1() == NEQ);   // NEQ is stricter for lower
    assert(result.get_deviation2() == 50);   // min(100, 50) = 50
    assert(result.get_equality2() == NEQ);   // NEQ is stricter for upper
    
    std::cout << "  Merged: " << result.to_string() << std::endl;
    std::cout << "  ✓ Open/closed interval test passed" << std::endl;
}

void test_equality_constraints() {
    std::cout << "\n=== Testing Equality Constraint Merge ===" << std::endl;
    
    // C1: x = B (i.e., x IN [B+0, B+0])
    JoinConstraint c1 = JoinConstraint::equality("A", "x", "B", "y");
    
    // C2: x IN [B-10, B+10]
    JoinConstraint c2(
        "A", "x", "B", "y",
        -10, EQ, 10, EQ
    );
    
    std::cout << "  C1: " << c1.to_string() << std::endl;
    std::cout << "  C2: " << c2.to_string() << std::endl;
    
    // Merged should be x = B (the stricter constraint)
    auto merged = ConditionMerger::merge(c1, c2);
    assert(merged.has_value());
    
    JoinConstraint result = merged.value();
    assert(result.get_deviation1() == 0);
    assert(result.get_deviation2() == 0);
    assert(result.is_equality());
    
    std::cout << "  Merged: " << result.to_string() << std::endl;
    std::cout << "  ✓ Equality constraint merge passed" << std::endl;
}

void test_cannot_merge_different_columns() {
    std::cout << "\n=== Testing Different Columns Cannot Merge ===" << std::endl;
    
    JoinConstraint c1("A", "x", "B", "y", 0, EQ, 100, EQ);
    JoinConstraint c2("A", "z", "B", "y", -100, EQ, 0, EQ);  // Different source column
    
    assert(!ConditionMerger::can_merge(c1, c2));
    
    auto merged = ConditionMerger::merge(c1, c2);
    assert(!merged.has_value());
    
    std::cout << "  ✓ Different columns cannot merge test passed" << std::endl;
}

void test_one_sided_ranges() {
    std::cout << "\n=== Testing One-sided Ranges ===" << std::endl;
    
    // C1: A.x > B.y (i.e., A.x IN (B.y, +∞])
    JoinConstraint c1(
        "A", "x", "B", "y",
        0, NEQ,                      // open lower bound at B.y
        JOIN_ATTR_POS_INF, EQ        // +∞
    );
    
    // C2: A.x <= B.y + 100 (i.e., A.x IN [-∞, B.y+100])
    JoinConstraint c2(
        "A", "x", "B", "y",
        JOIN_ATTR_NEG_INF, EQ,       // -∞
        100, EQ                      // closed upper bound
    );
    
    std::cout << "  C1 (A.x > B.y): " << c1.to_string() << std::endl;
    std::cout << "  C2 (A.x <= B.y+100): " << c2.to_string() << std::endl;
    
    // Merged should be (B.y, B.y+100]
    auto merged = ConditionMerger::merge(c1, c2);
    assert(merged.has_value());
    
    JoinConstraint result = merged.value();
    assert(result.get_deviation1() == 0);
    assert(result.get_equality1() == NEQ);  // Open lower
    assert(result.get_deviation2() == 100);
    assert(result.get_equality2() == EQ);   // Closed upper
    
    std::cout << "  Merged: " << result.to_string() << std::endl;
    std::cout << "  ✓ One-sided ranges test passed" << std::endl;
}

void test_tpch_tb2_example() {
    std::cout << "\n=== Testing TPC-H TB2 Query Example ===" << std::endl;
    
    // From tpch_tb2.sql:
    // part2.P2_P_RETAILPRICE >= part1.P1_P_RETAILPRICE - 50
    JoinConstraint c1(
        "part2", "P2_P_RETAILPRICE",
        "part1", "P1_P_RETAILPRICE",
        -50, EQ,
        JOIN_ATTR_POS_INF, EQ
    );
    
    // part2.P2_P_RETAILPRICE <= part1.P1_P_RETAILPRICE + 40
    JoinConstraint c2(
        "part2", "P2_P_RETAILPRICE",
        "part1", "P1_P_RETAILPRICE",
        JOIN_ATTR_NEG_INF, EQ,
        40, EQ
    );
    
    std::cout << "  C1 (>= P1-50): " << c1.to_string() << std::endl;
    std::cout << "  C2 (<= P1+40): " << c2.to_string() << std::endl;
    
    // Merged should be [P1-50, P1+40]
    auto merged = ConditionMerger::merge(c1, c2);
    assert(merged.has_value());
    
    JoinConstraint result = merged.value();
    assert(result.get_deviation1() == -50);
    assert(result.get_deviation2() == 40);
    assert(result.get_equality1() == EQ);
    assert(result.get_equality2() == EQ);
    
    std::cout << "  Merged: " << result.to_string() << std::endl;
    std::cout << "  Expected: P2_P_RETAILPRICE IN [P1_P_RETAILPRICE-50, P1_P_RETAILPRICE+40]" << std::endl;
    std::cout << "  ✓ TPC-H TB2 example passed" << std::endl;
}

int main() {
    std::cout << "Condition Merger Unit Tests" << std::endl;
    std::cout << "===========================" << std::endl;
    
    try {
        test_basic_band_merge();
        test_overlapping_ranges();
        test_no_overlap();
        test_open_closed_intervals();
        test_equality_constraints();
        test_cannot_merge_different_columns();
        test_one_sided_ranges();
        test_tpch_tb2_example();
        
        std::cout << "\n=== All merger tests passed! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}