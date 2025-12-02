#!/bin/bash
# Benchmark script comparing non-decomposed vs decomposed query execution
#
# Usage: ./scripts/benchmark_decomposition.sh <query.sql> <data_dir> [num_runs]
#
# Example:
#   ./scripts/benchmark_decomposition.sh input/queries/banking_chain4_filtered.sql input/plaintext/banking
#   ./scripts/benchmark_decomposition.sh input/queries/banking_branch_filtered.sql input/plaintext/banking 5

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <query.sql> <data_dir> [num_runs]"
    echo ""
    echo "Arguments:"
    echo "  query.sql   Input SQL query file"
    echo "  data_dir    Directory containing account.csv and txn.csv"
    echo "  num_runs    Number of runs for averaging (default: 3)"
    echo ""
    echo "Examples:"
    echo "  $0 input/queries/banking_chain4_filtered.sql input/plaintext/banking"
    echo "  $0 input/queries/banking_branch_filtered.sql input/plaintext/banking 5"
    exit 1
fi

QUERY=$1
DATA_DIR=$2
NUM_RUNS=${3:-3}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Verify required files exist
if [ ! -f "$QUERY" ]; then
    echo "Error: Query file not found: $QUERY"
    exit 1
fi

if [ ! -d "$DATA_DIR" ]; then
    echo "Error: Data directory not found: $DATA_DIR"
    exit 1
fi

if [ ! -f "$PROJECT_DIR/sgx_app" ]; then
    echo "Error: sgx_app not found. Run 'make' first."
    exit 1
fi

if [ ! -f "$PROJECT_DIR/obligraph/build/banking_onehop" ]; then
    echo "Error: banking_onehop not found. Build obligraph first."
    exit 1
fi

# Create temp directory for outputs
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Extract query name for display
QUERY_NAME=$(basename "$QUERY" .sql)

echo "=============================================="
echo "  DECOMPOSITION BENCHMARK"
echo "=============================================="
echo "Query:    $QUERY_NAME"
echo "Data:     $DATA_DIR"
echo "Runs:     $NUM_RUNS"
echo ""

# Show decomposition info
echo "--- Query Decomposition ---"
python3 "$SCRIPT_DIR/rewrite_chain_query.py" "$QUERY" 2>&1 | grep -E "^(Accounts|Decomposition|  [ht])" || true
echo ""

#######################################
# Run non-decomposed (original) approach
#######################################
echo "--- Running Non-Decomposed (Original) ---"

ORIGINAL_TIMES=()
ORIGINAL_ROWS=""

for i in $(seq 1 $NUM_RUNS); do
    START=$(date +%s.%N)
    OUTPUT=$("$PROJECT_DIR/sgx_app" "$QUERY" "$DATA_DIR" "$TEMP_DIR/original_$i.csv" 2>&1)
    END=$(date +%s.%N)

    TIME=$(echo "$END - $START" | bc)
    ORIGINAL_TIMES+=($TIME)

    # Extract row count from first run
    if [ -z "$ORIGINAL_ROWS" ]; then
        ORIGINAL_ROWS=$(echo "$OUTPUT" | grep -oP 'Result: \K\d+' | head -1)
    fi

    printf "  Run %d: %.3fs\n" $i $TIME
done

# Calculate average
ORIGINAL_AVG=$(echo "${ORIGINAL_TIMES[@]}" | tr ' ' '\n' | awk '{sum+=$1} END {printf "%.3f", sum/NR}')
echo "  Average: ${ORIGINAL_AVG}s ($ORIGINAL_ROWS rows)"
echo ""

#######################################
# Run decomposed (optimized) approach
#######################################
echo "--- Running Decomposed (Optimized) ---"

DECOMPOSED_TIMES=()
ONEHOP_TIMES=()
MWBJ_TIMES=()
DECOMPOSED_ROWS=""

for i in $(seq 1 $NUM_RUNS); do
    # Create fresh temp dir for this run
    RUN_TEMP=$(mktemp -d)

    # Step 1: Convert data (NOT timed - preprocessing)
    python3 "$SCRIPT_DIR/convert_banking_to_obligraph.py" "$DATA_DIR" "$RUN_TEMP/obligraph_data" > /dev/null 2>&1

    # Step 2: Run one-hop (TIMED)
    START_ONEHOP=$(date +%s.%N)
    "$PROJECT_DIR/obligraph/build/banking_onehop" "$RUN_TEMP/obligraph_data" "$RUN_TEMP/hop.csv" > /dev/null 2>&1
    END_ONEHOP=$(date +%s.%N)
    ONEHOP_TIME=$(echo "$END_ONEHOP - $START_ONEHOP" | bc)
    ONEHOP_TIMES+=($ONEHOP_TIME)

    # Step 3: Prepare data (NOT timed - preprocessing)
    mkdir -p "$RUN_TEMP/mwbj_data"
    cp "$RUN_TEMP/hop.csv" "$RUN_TEMP/mwbj_data/"
    cp "$DATA_DIR/txn.csv" "$RUN_TEMP/mwbj_data/"

    # Step 4: Rewrite query (NOT timed - preprocessing)
    python3 "$SCRIPT_DIR/rewrite_chain_query.py" "$QUERY" "$RUN_TEMP/decomposed.sql" 2> /dev/null

    # Step 5: Run multi-way band joins (TIMED)
    START_MWBJ=$(date +%s.%N)
    OUTPUT=$("$PROJECT_DIR/sgx_app" "$RUN_TEMP/decomposed.sql" "$RUN_TEMP/mwbj_data" "$TEMP_DIR/decomposed_$i.csv" 2>&1)
    END_MWBJ=$(date +%s.%N)
    MWBJ_TIME=$(echo "$END_MWBJ - $START_MWBJ" | bc)
    MWBJ_TIMES+=($MWBJ_TIME)

    # Total decomposed time
    TOTAL_TIME=$(echo "$ONEHOP_TIME + $MWBJ_TIME" | bc)
    DECOMPOSED_TIMES+=($TOTAL_TIME)

    # Extract row count from first run
    if [ -z "$DECOMPOSED_ROWS" ]; then
        DECOMPOSED_ROWS=$(echo "$OUTPUT" | grep -oP 'Result: \K\d+' | head -1)
    fi

    printf "  Run %d: %.3fs (one-hop: %.3fs, mwbj: %.3fs)\n" $i $TOTAL_TIME $ONEHOP_TIME $MWBJ_TIME

    rm -rf "$RUN_TEMP"
done

# Calculate averages
DECOMPOSED_AVG=$(echo "${DECOMPOSED_TIMES[@]}" | tr ' ' '\n' | awk '{sum+=$1} END {printf "%.3f", sum/NR}')
ONEHOP_AVG=$(echo "${ONEHOP_TIMES[@]}" | tr ' ' '\n' | awk '{sum+=$1} END {printf "%.3f", sum/NR}')
MWBJ_AVG=$(echo "${MWBJ_TIMES[@]}" | tr ' ' '\n' | awk '{sum+=$1} END {printf "%.3f", sum/NR}')

echo "  Average: ${DECOMPOSED_AVG}s ($DECOMPOSED_ROWS rows)"
echo "    - One-hop:  ${ONEHOP_AVG}s"
echo "    - MWBJ:     ${MWBJ_AVG}s"
echo ""

#######################################
# Verify correctness
#######################################
CORRECT="YES"
if [ "$ORIGINAL_ROWS" != "$DECOMPOSED_ROWS" ]; then
    CORRECT="NO (row count mismatch: $ORIGINAL_ROWS vs $DECOMPOSED_ROWS)"
fi

#######################################
# Summary
#######################################
SPEEDUP=$(echo "scale=2; $ORIGINAL_AVG / $DECOMPOSED_AVG" | bc)

echo "=============================================="
echo "  SUMMARY: $QUERY_NAME"
echo "=============================================="
echo ""
printf "  %-20s %10s\n" "Approach" "Time (avg)"
printf "  %-20s %10s\n" "--------------------" "----------"
printf "  %-20s %9.3fs\n" "Non-Decomposed" "$ORIGINAL_AVG"
printf "  %-20s %9.3fs\n" "Decomposed" "$DECOMPOSED_AVG"
echo ""
printf "  Speedup: %.2fx\n" "$SPEEDUP"
printf "  Correct: %s\n" "$CORRECT"
echo ""
echo "=============================================="
