#!/bin/bash

# Run all TPCH queries with specified dataset
# Usage: ./run_tpch_tests.sh [scale]
#   scale: data scale (0_001, 0_01, etc.) - default is 0_001

# Get script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
SCALE="${1:-0_001}"  # Default to 0_001 if not specified
QUERY_DIR="$PROJECT_ROOT/input/queries"
DATA_DIR="$PROJECT_ROOT/input/encrypted/data_$SCALE"
SRC_DIR="$PROJECT_ROOT/impl/src"
TEST_PROG="$SRC_DIR/test/test_join"
OUTPUT_DIR="$PROJECT_ROOT/output"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if test_join exists
if [ ! -f "$TEST_PROG" ]; then
    echo -e "${RED}Error: test_join not found at $TEST_PROG${NC}"
    echo -e "${YELLOW}Please build first: $SCRIPT_DIR/build.sh --test${NC}"
    exit 1
fi

# Check if data directory exists
if [ ! -d "$DATA_DIR" ]; then
    echo -e "${RED}Error: Data directory not found: $DATA_DIR${NC}"
    echo -e "${YELLOW}Available data scales:${NC}"
    ls -d "$PROJECT_ROOT/input/encrypted/data_"* 2>/dev/null | xargs -n1 basename
    exit 1
fi

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

echo -e "${GREEN}=== Running TPCH Tests ===${NC}"
echo "Data scale: $SCALE"
echo "Data directory: $DATA_DIR"
echo "Query directory: $QUERY_DIR"
echo ""

# Track test results
PASSED=0
FAILED=0
TOTAL=0

# Process each SQL file
for query_file in $QUERY_DIR/*.sql; do
    if [ -f "$query_file" ]; then
        query_name=$(basename "$query_file" .sql)
        TOTAL=$((TOTAL + 1))
        
        echo "----------------------------------------"
        echo -e "${GREEN}Running: $query_name${NC}"
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