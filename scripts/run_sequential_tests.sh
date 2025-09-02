#!/bin/bash

# Sequential Test Runner for TPC-H Benchmarks
# Usage: ./run_sequential_tests.sh data_scale1 [data_scale2 ...]
#
# Examples:
#   ./run_sequential_tests.sh 0_001              # Run all queries on data_0_001
#   ./run_sequential_tests.sh 0_001 0_01         # Run all queries on both datasets
#
# This script runs all TPC-H queries sequentially on specified datasets
# to avoid resource contention and get consistent timing results.

# Get script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
QUERIES=(tpch_tb1 tpch_tb2 tpch_tm1 tpch_tm2 tpch_tm3)
QUERY_DIR="$PROJECT_ROOT/input/queries"
DATA_BASE="$PROJECT_ROOT/input/encrypted"
OUTPUT_BASE="$PROJECT_ROOT/output"
SRC_DIR="$PROJECT_ROOT/impl/src"
TEST_TOOL="$SRC_DIR/test/test_join"
TIMEOUT=7200  # 2 hour timeout per test

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print usage
print_usage() {
    echo "Usage: $0 data_scale1 [data_scale2 ...]"
    echo ""
    echo "Arguments:"
    echo "  data_scale   Data scale factor (e.g., 0_001, 0_01, 0_1)"
    echo ""
    echo "Examples:"
    echo "  $0 0_001              # Run all queries on data_0_001"
    echo "  $0 0_001 0_01         # Run all queries on both datasets"
    echo ""
    echo "Available data scales:"
    ls -d "$DATA_BASE/data_"* 2>/dev/null | xargs -n1 basename | sed 's/data_//'
}

# Check if at least one data scale is provided
if [ $# -eq 0 ]; then
    print_usage
    exit 1
fi

# Check if test_join exists
if [ ! -f "$TEST_TOOL" ]; then
    echo -e "${RED}Error: test_join not found at $TEST_TOOL${NC}"
    echo -e "${YELLOW}Please build first: $SCRIPT_DIR/build.sh --test${NC}"
    exit 1
fi

# Validate all data directories exist
DATA_SCALES=()
for scale in "$@"; do
    DATA_DIR="$DATA_BASE/data_$scale"
    if [ ! -d "$DATA_DIR" ]; then
        echo -e "${RED}Error: Data directory not found: $DATA_DIR${NC}"
        echo ""
        print_usage
        exit 1
    fi
    DATA_SCALES+=("$scale")
done

# Create run-specific output directory
RUN_ID=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$OUTPUT_BASE/run_${RUN_ID}_sequential"
mkdir -p "$OUTPUT_DIR"

# Track overall statistics
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
START_TIME=$(date +%s)

# Create results summary file
SUMMARY_FILE="$OUTPUT_DIR/summary.csv"
echo "Dataset,Query,Status,Time(s),SGX_Rows,SQLite_Rows,Match" > "$SUMMARY_FILE"

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}Sequential TPC-H Benchmark Tests${NC}"
echo "Run ID: $RUN_ID"
echo "Queries: ${QUERIES[*]}"
echo "Datasets: ${DATA_SCALES[*]}"
echo "Timeout: $((TIMEOUT/60)) minutes per test"
echo "Output: $OUTPUT_DIR"
echo -e "${BLUE}================================================${NC}"
echo ""

# Log file for this run
LOG_FILE="$OUTPUT_DIR/run.log"
echo "Sequential test run started at $(date)" > "$LOG_FILE"
echo "Queries: ${QUERIES[*]}" >> "$LOG_FILE"
echo "Datasets: ${DATA_SCALES[*]}" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

# Function to run a single test
run_test() {
    local data_scale=$1
    local query=$2
    local data_dir="$DATA_BASE/data_$data_scale"
    local query_file="$QUERY_DIR/${query}.sql"
    local output_file="$OUTPUT_DIR/${query}_data_${data_scale}.txt"
    
    echo -e "${GREEN}Testing: $query on data_$data_scale${NC}"
    
    if [ ! -f "$query_file" ]; then
        echo -e "${RED}  ✗ Query file not found: $query_file${NC}"
        echo "$data_scale,$query,SKIP,0,0,0,N/A" >> "$SUMMARY_FILE"
        return 2
    fi
    
    # Run the test with timeout
    local test_start=$(date +%s)
    timeout $TIMEOUT $TEST_TOOL "$query_file" "$data_dir" > "$output_file" 2>&1
    local result=$?
    local test_end=$(date +%s)
    local test_duration=$((test_end - test_start))
    
    # Parse output for results
    local sgx_rows=$(grep "SGX=" "$output_file" | sed -n 's/.*SGX=\([0-9]*\).*/\1/p')
    local sqlite_rows=$(grep "SQLite=" "$output_file" | sed -n 's/.*SQLite=\([0-9]*\).*/\1/p')
    local match=$(grep "Match:" "$output_file" | sed -n 's/.*Match: \(.*\)/\1/p')
    
    # Default values if parsing fails
    sgx_rows=${sgx_rows:-0}
    sqlite_rows=${sqlite_rows:-0}
    match=${match:-"N/A"}
    
    # Check result
    if [ $result -eq 124 ]; then
        echo -e "${RED}  ✗ TIMEOUT after $((TIMEOUT/60)) minutes${NC}"
        echo "$data_scale,$query,TIMEOUT,$test_duration,$sgx_rows,$sqlite_rows,$match" >> "$SUMMARY_FILE"
        echo "TIMEOUT: $query on data_$data_scale after $TIMEOUT seconds" >> "$LOG_FILE"
        return 1
    elif [ $result -eq 0 ]; then
        if [ "$match" == "YES" ]; then
            echo -e "${GREEN}  ✓ PASSED in ${test_duration}s (SGX=$sgx_rows, SQLite=$sqlite_rows)${NC}"
            echo "$data_scale,$query,PASS,$test_duration,$sgx_rows,$sqlite_rows,$match" >> "$SUMMARY_FILE"
            echo "PASS: $query on data_$data_scale in ${test_duration}s" >> "$LOG_FILE"
            return 0
        else
            echo -e "${RED}  ✗ FAILED - Results mismatch${NC}"
            echo "$data_scale,$query,FAIL,$test_duration,$sgx_rows,$sqlite_rows,$match" >> "$SUMMARY_FILE"
            echo "FAIL: $query on data_$data_scale - mismatch" >> "$LOG_FILE"
            return 1
        fi
    else
        echo -e "${RED}  ✗ FAILED with error code $result${NC}"
        echo "$data_scale,$query,ERROR,$test_duration,$sgx_rows,$sqlite_rows,$match" >> "$SUMMARY_FILE"
        echo "ERROR: $query on data_$data_scale with code $result" >> "$LOG_FILE"
        
        # Show last few lines of error
        echo "  Error output:"
        tail -3 "$output_file" | sed 's/^/    /'
        return 1
    fi
}

# Run tests sequentially for each dataset
for data_scale in "${DATA_SCALES[@]}"; do
    echo ""
    echo -e "${BLUE}Dataset: data_$data_scale${NC}"
    echo "----------------------------------------"
    
    for query in "${QUERIES[@]}"; do
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        
        if run_test "$data_scale" "$query"; then
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            if [ $? -eq 2 ]; then
                SKIPPED_TESTS=$((SKIPPED_TESTS + 1))
            else
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
        fi
        
        # Small delay between tests to avoid resource issues
        sleep 2
    done
done

# Calculate total runtime
END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))
HOURS=$((TOTAL_TIME / 3600))
MINUTES=$(((TOTAL_TIME % 3600) / 60))
SECONDS=$((TOTAL_TIME % 60))

# Print summary
echo ""
echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}================================================${NC}"
echo "Total tests: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "Failed: ${RED}$FAILED_TESTS${NC}"
else
    echo "Failed: $FAILED_TESTS"
fi
if [ $SKIPPED_TESTS -gt 0 ]; then
    echo -e "Skipped: ${YELLOW}$SKIPPED_TESTS${NC}"
fi
echo "Total runtime: ${HOURS}h ${MINUTES}m ${SECONDS}s"
echo ""
echo "Results saved to: $OUTPUT_DIR"
echo "Summary CSV: $SUMMARY_FILE"
echo "Log file: $LOG_FILE"

# Write final summary to log
echo "" >> "$LOG_FILE"
echo "Summary: $PASSED_TESTS/$TOTAL_TESTS passed, $FAILED_TESTS failed, $SKIPPED_TESTS skipped" >> "$LOG_FILE"
echo "Total runtime: ${HOURS}h ${MINUTES}m ${SECONDS}s" >> "$LOG_FILE"
echo "Sequential test run completed at $(date)" >> "$LOG_FILE"

# Show quick results table
echo ""
echo "Quick Results:"
echo "-------------"
column -t -s',' "$SUMMARY_FILE" | head -20

# Exit with appropriate code
if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
else
    exit 0
fi