#!/bin/bash

# Run tests on small banking dataset (100 accounts, 200 txns)
# This dataset is suitable for complex queries like triple self-joins
# Usage: ./run_banking_small_tests.sh

# Get script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
QUERY_DIR="$PROJECT_ROOT/input/queries"
DATA_DIR="$PROJECT_ROOT/input/plaintext/banking_small"
TEST_PROG="$PROJECT_ROOT/test_join"
OUTPUT_DIR="$PROJECT_ROOT/output"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if test_join exists
if [ ! -f "$TEST_PROG" ]; then
    echo -e "${RED}Error: test_join not found at $TEST_PROG${NC}"
    echo -e "${YELLOW}Please build first: make tests${NC}"
    exit 1
fi

# Check if data directory exists
if [ ! -d "$DATA_DIR" ]; then
    echo -e "${RED}Error: Data directory not found: $DATA_DIR${NC}"
    echo -e "${YELLOW}Generate dataset first: python3 scripts/generate_banking_small.py${NC}"
    exit 1
fi

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

echo -e "${GREEN}=== Running Small Banking Dataset Tests ===${NC}"
echo "Dataset: banking_small (100 accounts, 200 transactions)"
echo "Data directory: $DATA_DIR"
echo "Query directory: $QUERY_DIR"
echo ""

# Track test results
PASSED=0
FAILED=0
TOTAL=0

# Test queries suitable for small dataset
QUERIES=(
    "banking_simple.sql"
    "banking_account_to_account.sql"
    "test_triple_self_join.sql"
)

for query_name in "${QUERIES[@]}"; do
    query_file="$QUERY_DIR/$query_name"
    if [ -f "$query_file" ]; then
        TOTAL=$((TOTAL + 1))
        query_base=$(basename "$query_name" .sql)

        echo "----------------------------------------"
        echo -e "${GREEN}Running: $query_base${NC}"
        echo "----------------------------------------"

        # Run test_join and capture output
        OUTPUT=$($TEST_PROG "$query_file" "$DATA_DIR" 2>&1)
        RESULT=$?

        # Show last 15 lines of output
        echo "$OUTPUT" | tail -15

        # Check if test passed
        if echo "$OUTPUT" | grep -q "Match: YES"; then
            echo -e "${GREEN}✓ Test PASSED${NC}"
            PASSED=$((PASSED + 1))
        elif [ $RESULT -eq 0 ]; then
            echo -e "${YELLOW}⚠ Test completed (no comparison)${NC}"
            PASSED=$((PASSED + 1))
        else
            echo -e "${RED}✗ Test FAILED${NC}"
            FAILED=$((FAILED + 1))
        fi

        echo ""
    else
        echo -e "${YELLOW}Warning: Query file not found: $query_file${NC}"
    fi
done

# Summary
echo -e "${GREEN}=== Test Summary ===${NC}"
echo "Total tests: $TOTAL"
echo -e "Passed: ${GREEN}$PASSED${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "Failed: ${RED}$FAILED${NC}"
else
    echo -e "Failed: $FAILED"
fi

# Check for output files
if ls "$OUTPUT_DIR"/*.txt >/dev/null 2>&1; then
    echo ""
    echo "Summary files written to: $OUTPUT_DIR/"
    ls -la "$OUTPUT_DIR"/*.txt 2>/dev/null | tail -5
fi

# Exit with appropriate code
if [ $FAILED -gt 0 ]; then
    exit 1
else
    exit 0
fi
