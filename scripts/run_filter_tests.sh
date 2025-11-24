#!/bin/bash
# =============================================================================
# run_filter_tests.sh
# Test script for oblivious filtering functionality
# Compares SGX output with SQLite baseline
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

TEST_PROG="$PROJECT_ROOT/test_join"
QUERY_DIR="$PROJECT_ROOT/input/queries"
DATA_DIR="$PROJECT_ROOT/input/plaintext/banking_small"

# Check if test program exists
if [[ ! -x "$TEST_PROG" ]]; then
    echo "Error: test_join not found. Run 'make test_join' first."
    exit 1
fi

# Check if data directory exists
if [[ ! -d "$DATA_DIR" ]]; then
    echo "Error: Data directory not found: $DATA_DIR"
    exit 1
fi

# Filter test queries (in order of complexity)
FILTER_TESTS=(
    "filter_01_parent_only.sql"
    "filter_02_child_only.sql"
    "filter_03_both_tables.sql"
    "filter_04_no_match.sql"
    "filter_05_all_match.sql"
    "filter_06_single_match.sql"
    "filter_07_on_join_column.sql"
    "filter_08_on_non_join_column.sql"
    "filter_09_three_way_root.sql"
    "filter_10_three_way_leaf.sql"
    "filter_11_self_join_one_alias.sql"
)

PASSED=0
FAILED=0
SKIPPED=0
TOTAL=${#FILTER_TESTS[@]}

echo "========================================"
echo "  Oblivious Filtering Test Suite"
echo "========================================"
echo "Data directory: $DATA_DIR"
echo "Total tests: $TOTAL"
echo "----------------------------------------"

for query in "${FILTER_TESTS[@]}"; do
    echo ""
    echo "Test: $query"

    if [[ ! -f "$QUERY_DIR/$query" ]]; then
        echo "  ⊘ SKIPPED (query file not found)"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # Run test and capture output (don't exit on error)
    OUTPUT=$("$TEST_PROG" "$QUERY_DIR/$query" "$DATA_DIR" 2>&1) || true

    if echo "$OUTPUT" | grep -q "Match: YES"; then
        echo "  ✓ PASSED"
        PASSED=$((PASSED + 1))

        # Show result summary
        SGX_ROWS=$(echo "$OUTPUT" | grep "SGX Output Size" | awk '{print $4}')
        SQLITE_ROWS=$(echo "$OUTPUT" | grep "SQLite Output Size" | awk '{print $4}')
        echo "    SGX: $SGX_ROWS rows, SQLite: $SQLITE_ROWS rows"
    else
        echo "  ✗ FAILED"
        FAILED=$((FAILED + 1))

        # Show failure details
        echo "  --- Output (last 25 lines) ---"
        echo "$OUTPUT" | tail -25 | sed 's/^/    /'
        echo "  ------------------------------"
    fi
done

echo ""
echo "========================================"
echo "  Summary"
echo "========================================"
echo "  Passed:  $PASSED / $TOTAL"
echo "  Failed:  $FAILED / $TOTAL"
echo "  Skipped: $SKIPPED / $TOTAL"
echo "========================================"

if [[ $FAILED -gt 0 ]]; then
    echo ""
    echo "Some tests FAILED. This is expected before implementation."
    exit 1
else
    echo ""
    echo "All tests PASSED!"
    exit 0
fi
