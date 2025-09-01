#!/bin/bash

# Sequential Test Runner for TPC-H Benchmarks
# Usage: ./run_sequential_tests.sh data_0_001 data_0_01 [data_0_1 data_1]
#
# This script runs all TPC-H queries sequentially on specified datasets
# to avoid resource contention and get consistent timing results.

# Configuration
QUERIES=(tpch_tb1 tpch_tb2 tpch_tm1 tpch_tm2 tpch_tm3)
BASE_DIR="/home/r33wei/omwj/memory_const"
QUERY_DIR="$BASE_DIR/input/queries"
DATA_BASE="$BASE_DIR/input/encrypted"
OUTPUT_BASE="$BASE_DIR/output"
TEST_TOOL="./test/test_join"
TIMEOUT=7200  # 2 hour timeout per test

# Create run-specific output directory
RUN_ID=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$OUTPUT_BASE/run_$RUN_ID"
mkdir -p "$OUTPUT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if at least one data directory is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 data_dir1 [data_dir2 ...]"
    echo "Example: $0 data_0_001 data_0_01"
    echo "Available data directories in $DATA_BASE:"
    ls -d "$DATA_BASE"/data_* 2>/dev/null | xargs -n1 basename
    exit 1
fi

# Track overall statistics
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
START_TIME=$(date +%s)

echo "================================================"
echo "Starting Sequential TPC-H Benchmark Tests"
echo "Run ID: $RUN_ID"
echo "Queries: ${QUERIES[*]}"
echo "Datasets: $*"
echo "Output: $OUTPUT_DIR"
echo "================================================"
echo ""

# Process each data directory
for data_dir in "$@"; do
    FULL_DATA_PATH="$DATA_BASE/$data_dir"
    
    # Check if data directory exists
    if [ ! -d "$FULL_DATA_PATH" ]; then
        echo -e "${RED}Error: Data directory not found: $FULL_DATA_PATH${NC}"
        continue
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
        TEST_START=$(date +%s)
        
        # Run the test with timeout
        timeout $TIMEOUT $TEST_TOOL "$QUERY_FILE" "$FULL_DATA_PATH"
        RESULT=$?
        
        TEST_END=$(date +%s)
        TEST_DURATION=$((TEST_END - TEST_START))
        
        if [ $RESULT -eq 0 ]; then
            echo -e "${GREEN}  ✓ $query completed successfully (${TEST_DURATION}s)${NC}"
            PASSED_TESTS=$((PASSED_TESTS + 1))
            
            # Move summary file to run-specific directory
            SUMMARY_FILE="$OUTPUT_BASE/${query}_${data_dir}_summary.txt"
            RUN_SUMMARY_FILE="$OUTPUT_DIR/${query}_${data_dir}_summary.txt"
            if [ -f "$SUMMARY_FILE" ]; then
                mv "$SUMMARY_FILE" "$RUN_SUMMARY_FILE"
                echo "    Summary saved to: run_$RUN_ID/$(basename $RUN_SUMMARY_FILE)"
            fi
        elif [ $RESULT -eq 124 ]; then
            echo -e "${RED}  ✗ $query timed out after ${TIMEOUT}s${NC}"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        else
            echo -e "${RED}  ✗ $query failed with exit code $RESULT (${TEST_DURATION}s)${NC}"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
        echo ""
    done
    
    echo "Completed testing on $data_dir"
    echo "----------------------------------------"
    echo ""
done

# Calculate total runtime
END_TIME=$(date +%s)
TOTAL_DURATION=$((END_TIME - START_TIME))
HOURS=$((TOTAL_DURATION / 3600))
MINUTES=$(((TOTAL_DURATION % 3600) / 60))
SECONDS=$((TOTAL_DURATION % 60))

# Print summary
echo "================================================"
echo "Test Summary"
echo "================================================"
echo "Total tests: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"
echo "Total runtime: ${HOURS}h ${MINUTES}m ${SECONDS}s"
echo ""

# List all generated summary files
echo "Generated summary files in: $OUTPUT_DIR"
for data_dir in "$@"; do
    for query in "${QUERIES[@]}"; do
        RUN_SUMMARY_FILE="$OUTPUT_DIR/${query}_${data_dir}_summary.txt"
        if [ -f "$RUN_SUMMARY_FILE" ]; then
            echo "  - $(basename $RUN_SUMMARY_FILE)"
        fi
    done
done

echo ""
echo "All tests completed!"

# Exit with non-zero if any tests failed
if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
fi