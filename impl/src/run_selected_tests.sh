#!/bin/bash

# Selective Test Runner for specific TPC-H queries
# Usage: ./run_selected_tests.sh data_0_1
#
# This script runs only TB1 and TM2 queries on specified dataset
# with extended timeout for large scale factors

# Configuration
QUERIES=(tpch_tb1 tpch_tm2)  # Only TB1 and TM2
BASE_DIR="/home/r33wei/omwj/memory_const"
QUERY_DIR="$BASE_DIR/input/queries"
DATA_BASE="$BASE_DIR/input/encrypted"
OUTPUT_BASE="$BASE_DIR/output"
TEST_TOOL="./test/test_join"
TIMEOUT=18000  # 5 hour timeout per test

# Create run-specific output directory
RUN_ID=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$OUTPUT_BASE/run_${RUN_ID}_selective"
mkdir -p "$OUTPUT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if at least one data directory is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 data_dir"
    echo "Example: $0 data_0_1"
    exit 1
fi

# Track overall statistics
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
START_TIME=$(date +%s)

echo "================================================"
echo "Selective TPC-H Benchmark Tests (TB1 & TM2)"
echo "Run ID: $RUN_ID"
echo "Queries: ${QUERIES[*]}"
echo "Dataset: $1"
echo "Timeout: $((TIMEOUT/3600)) hours per test"
echo "Output: $OUTPUT_DIR"
echo "================================================"
echo ""

# Process the data directory
data_dir="$1"
FULL_DATA_PATH="$DATA_BASE/$data_dir"

# Check if data directory exists
if [ ! -d "$FULL_DATA_PATH" ]; then
    echo -e "${RED}Error: Data directory not found: $FULL_DATA_PATH${NC}"
    exit 1
fi

echo "=== Testing dataset: $data_dir ==="
echo "Data path: $FULL_DATA_PATH"
echo ""

# Run each query
for query in "${QUERIES[@]}"; do
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    QUERY_FILE="$QUERY_DIR/$query.sql"
    
    # Check if query file exists
    if [ ! -f "$QUERY_FILE" ]; then
        echo -e "${RED}  ✗ Query file not found: $QUERY_FILE${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        continue
    fi
    
    echo -e "${YELLOW}  Running $query on $data_dir...${NC}"
    echo "  Expected output size:"
    if [ "$query" = "tpch_tb1" ] && [ "$data_dir" = "data_0_1" ]; then
        echo "    ~499,499 rows"
    elif [ "$query" = "tpch_tm2" ] && [ "$data_dir" = "data_0_1" ]; then
        echo "    ~2,999,594 rows"
    fi
    
    TEST_START=$(date +%s)
    
    # Run the test with timeout
    timeout $TIMEOUT $TEST_TOOL "$QUERY_FILE" "$FULL_DATA_PATH"
    RESULT=$?
    
    TEST_END=$(date +%s)
    TEST_DURATION=$((TEST_END - TEST_START))
    TEST_MINUTES=$((TEST_DURATION / 60))
    TEST_SECONDS=$((TEST_DURATION % 60))
    
    if [ $RESULT -eq 0 ]; then
        echo -e "${GREEN}  ✓ $query completed successfully (${TEST_MINUTES}m ${TEST_SECONDS}s)${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        
        # Move summary file to run-specific directory
        SUMMARY_FILE="$OUTPUT_BASE/${query}_${data_dir}_summary.txt"
        RUN_SUMMARY_FILE="$OUTPUT_DIR/${query}_${data_dir}_summary.txt"
        if [ -f "$SUMMARY_FILE" ]; then
            mv "$SUMMARY_FILE" "$RUN_SUMMARY_FILE"
            echo "    Summary saved to: run_${RUN_ID}_selective/$(basename $RUN_SUMMARY_FILE)"
        fi
    elif [ $RESULT -eq 124 ]; then
        echo -e "${RED}  ✗ $query timed out after ${TIMEOUT}s (${TIMEOUT/3600} hours)${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    else
        echo -e "${RED}  ✗ $query failed with exit code $RESULT (${TEST_MINUTES}m ${TEST_SECONDS}s)${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    echo ""
done

echo "Completed testing on $data_dir"
echo "----------------------------------------"

# Calculate total runtime
END_TIME=$(date +%s)
TOTAL_DURATION=$((END_TIME - START_TIME))
HOURS=$((TOTAL_DURATION / 3600))
MINUTES=$(((TOTAL_DURATION % 3600) / 60))
SECONDS=$((TOTAL_DURATION % 60))

# Print summary
echo ""
echo "================================================"
echo "Test Summary"
echo "================================================"
echo "Total tests: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"
echo "Total runtime: ${HOURS}h ${MINUTES}m ${SECONDS}s"
echo ""

# List generated summary files
echo "Generated summary files in: $OUTPUT_DIR"
for query in "${QUERIES[@]}"; do
    RUN_SUMMARY_FILE="$OUTPUT_DIR/${query}_${data_dir}_summary.txt"
    if [ -f "$RUN_SUMMARY_FILE" ]; then
        echo "  - $(basename $RUN_SUMMARY_FILE)"
    fi
done

echo ""
echo "All tests completed!"

# Exit with non-zero if any tests failed
if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
fi