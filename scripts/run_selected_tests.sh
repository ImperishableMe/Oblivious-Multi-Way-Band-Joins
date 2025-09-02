#!/bin/bash

# Selective Test Runner for specific TPC-H queries
# Usage: ./run_selected_tests.sh <data_scale> [queries...]
#
# Examples:
#   ./run_selected_tests.sh 0_001            # Run TB1 and TM1 on data_0_001
#   ./run_selected_tests.sh 0_01 tb1 tb2     # Run TB1 and TB2 on data_0_01
#   ./run_selected_tests.sh 0_001 all        # Run all queries on data_0_001

# Get script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
DEFAULT_QUERIES=(tpch_tb1 tpch_tm1)  # Default: TB1 and TM1
QUERY_DIR="$PROJECT_ROOT/input/queries"
DATA_BASE="$PROJECT_ROOT/input/encrypted"
OUTPUT_BASE="$PROJECT_ROOT/output"
SRC_DIR="$PROJECT_ROOT/impl/src"
TEST_TOOL="$SRC_DIR/test/test_join"
TIMEOUT=18000  # 5 hour timeout per test

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print usage
print_usage() {
    echo "Usage: $0 <data_scale> [queries...]"
    echo ""
    echo "Arguments:"
    echo "  data_scale   Data scale factor (e.g., 0_001, 0_01, 0_1)"
    echo "  queries      Optional: specific queries to run (tb1, tb2, tm1, tm2, tm3, all)"
    echo ""
    echo "Examples:"
    echo "  $0 0_001                # Run default queries (TB1, TM1) on data_0_001"
    echo "  $0 0_01 tb1 tb2         # Run TB1 and TB2 on data_0_01"
    echo "  $0 0_001 all            # Run all queries on data_0_001"
    echo ""
    echo "Available data scales:"
    ls -d "$DATA_BASE/data_"* 2>/dev/null | xargs -n1 basename
}

# Check arguments
if [ $# -eq 0 ]; then
    print_usage
    exit 1
fi

# Parse arguments
DATA_SCALE=$1
shift

# Determine which queries to run
if [ $# -eq 0 ]; then
    # No queries specified, use defaults
    QUERIES=("${DEFAULT_QUERIES[@]}")
elif [ "$1" == "all" ]; then
    # Run all available queries
    QUERIES=()
    for query_file in $QUERY_DIR/tpch_*.sql; do
        if [ -f "$query_file" ]; then
            query_name=$(basename "$query_file" .sql)
            QUERIES+=("$query_name")
        fi
    done
else
    # Run specified queries
    QUERIES=()
    for query in "$@"; do
        # Normalize query name (tb1 -> tpch_tb1)
        if [[ ! "$query" =~ ^tpch_ ]]; then
            query="tpch_$query"
        fi
        QUERIES+=("$query")
    done
fi

# Verify data directory exists
DATA_DIR="$DATA_BASE/data_$DATA_SCALE"
if [ ! -d "$DATA_DIR" ]; then
    echo -e "${RED}Error: Data directory not found: $DATA_DIR${NC}"
    echo ""
    print_usage
    exit 1
fi

# Check if test_join exists
if [ ! -f "$TEST_TOOL" ]; then
    echo -e "${RED}Error: test_join not found at $TEST_TOOL${NC}"
    echo -e "${YELLOW}Please build first: $SCRIPT_DIR/build.sh --test${NC}"
    exit 1
fi

# Create run-specific output directory
RUN_ID=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$OUTPUT_BASE/run_${RUN_ID}_selective"
mkdir -p "$OUTPUT_DIR"

# Track overall statistics
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
START_TIME=$(date +%s)

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}Selective TPC-H Benchmark Tests${NC}"
echo "Run ID: $RUN_ID"
echo "Queries: ${QUERIES[*]}"
echo "Dataset: data_$DATA_SCALE"
echo "Timeout: $((TIMEOUT/3600)) hours per test"
echo "Output: $OUTPUT_DIR"
echo -e "${BLUE}================================================${NC}"
echo ""

# Log file for this run
LOG_FILE="$OUTPUT_DIR/run.log"
echo "Test run started at $(date)" > "$LOG_FILE"
echo "Queries: ${QUERIES[*]}" >> "$LOG_FILE"
echo "Dataset: data_$DATA_SCALE" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

# Function to run a single test
run_test() {
    local query=$1
    local query_file="$QUERY_DIR/${query}.sql"
    local output_file="$OUTPUT_DIR/${query}_data_${DATA_SCALE}.txt"
    
    if [ ! -f "$query_file" ]; then
        echo -e "${RED}Query file not found: $query_file${NC}"
        return 1
    fi
    
    echo -e "${GREEN}Running $query on data_$DATA_SCALE...${NC}"
    echo "Query file: $query_file"
    echo "Data dir: $DATA_DIR"
    echo "Output: $output_file"
    
    # Run the test with timeout
    local test_start=$(date +%s)
    timeout $TIMEOUT $TEST_TOOL "$query_file" "$DATA_DIR" > "$output_file" 2>&1
    local result=$?
    local test_end=$(date +%s)
    local test_duration=$((test_end - test_start))
    
    # Check result
    if [ $result -eq 124 ]; then
        echo -e "${RED}✗ TIMEOUT after $((TIMEOUT/3600)) hours${NC}"
        echo "TIMEOUT: $query on data_$DATA_SCALE after $TIMEOUT seconds" >> "$LOG_FILE"
        return 1
    elif [ $result -eq 0 ]; then
        # Check if test actually passed
        if grep -q "Match: YES" "$output_file"; then
            echo -e "${GREEN}✓ PASSED in ${test_duration}s${NC}"
            echo "PASSED: $query on data_$DATA_SCALE in ${test_duration}s" >> "$LOG_FILE"
            
            # Extract key metrics
            echo "  Results:"
            grep -E "(Result:|Output:|Time:)" "$output_file" | head -3
            return 0
        else
            echo -e "${RED}✗ FAILED - Results don't match${NC}"
            echo "FAILED: $query on data_$DATA_SCALE - mismatch" >> "$LOG_FILE"
            return 1
        fi
    else
        echo -e "${RED}✗ FAILED with error code $result${NC}"
        echo "FAILED: $query on data_$DATA_SCALE with code $result" >> "$LOG_FILE"
        
        # Show error details
        echo "  Error details:"
        tail -5 "$output_file"
        return 1
    fi
}

# Run all selected tests
for query in "${QUERIES[@]}"; do
    echo "----------------------------------------"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if run_test "$query"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    echo ""
done

# Calculate total runtime
END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))
HOURS=$((TOTAL_TIME / 3600))
MINUTES=$(((TOTAL_TIME % 3600) / 60))
SECONDS=$((TOTAL_TIME % 60))

# Print summary
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
echo "Total runtime: ${HOURS}h ${MINUTES}m ${SECONDS}s"
echo ""
echo "Results saved to: $OUTPUT_DIR"
echo "Log file: $LOG_FILE"

# Write summary to log
echo "" >> "$LOG_FILE"
echo "Summary: $PASSED_TESTS/$TOTAL_TESTS passed" >> "$LOG_FILE"
echo "Total runtime: ${HOURS}h ${MINUTES}m ${SECONDS}s" >> "$LOG_FILE"
echo "Test run completed at $(date)" >> "$LOG_FILE"

# Exit with appropriate code
if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
else
    exit 0
fi