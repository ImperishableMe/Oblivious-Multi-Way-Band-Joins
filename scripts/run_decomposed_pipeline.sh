#!/bin/bash
# Query decomposition pipeline
#
# Runs a banking chain/branch query using one-hop pre-computation.
# Reports only execution time (one-hop + multi-way band join).
#
# Usage: ./scripts/run_decomposed_pipeline.sh <query.sql> <data_dir> <output.csv>

set -e

if [ $# -lt 3 ]; then
    echo "Usage: $0 <query.sql> <data_dir> <output.csv>"
    echo "  query.sql: Input SQL query (e.g., input/queries/banking_chain4_filtered.sql)"
    echo "  data_dir:  Directory with account.csv and txn.csv"
    echo "  output.csv: Output file path"
    exit 1
fi

QUERY=$1
DATA_DIR=$2
OUTPUT=$3

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Verify required executables exist
if [ ! -f "$PROJECT_DIR/obligraph/build/banking_onehop" ]; then
    echo "Error: banking_onehop not found. Build obligraph first."
    exit 1
fi

if [ ! -f "$PROJECT_DIR/sgx_app" ]; then
    echo "Error: sgx_app not found. Build the main project first."
    exit 1
fi

# Create temp directory
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "=== QUERY DECOMPOSITION PIPELINE ==="
echo "Query: $QUERY"
echo "Data:  $DATA_DIR"
echo "Output: $OUTPUT"
echo ""

# Step 1: Convert data to ObliGraph format (NOT timed)
echo "[1/5] Converting data format..."
python3 "$SCRIPT_DIR/convert_banking_to_obligraph.py" "$DATA_DIR" "$TEMP_DIR/obligraph_data"

# Step 2: Run one-hop (TIMED)
echo ""
echo "[2/5] Running one-hop join..."
START_ONEHOP=$(date +%s.%N)
"$PROJECT_DIR/obligraph/build/banking_onehop" "$TEMP_DIR/obligraph_data" "$TEMP_DIR/hop.csv" 2>&1
END_ONEHOP=$(date +%s.%N)
ONEHOP_TIME=$(echo "$END_ONEHOP - $START_ONEHOP" | bc)

# Step 3: Prepare hop result for Multi-Way Band Joins (NOT timed)
echo ""
echo "[3/5] Preparing intermediate data..."
mkdir -p "$TEMP_DIR/mwbj_data"
cp "$TEMP_DIR/hop.csv" "$TEMP_DIR/mwbj_data/"

# Show hop result stats
HOP_ROWS=$(wc -l < "$TEMP_DIR/hop.csv")
echo "Hop result: $((HOP_ROWS - 1)) rows"

# Step 4: Rewrite query (NOT timed)
echo ""
echo "[4/5] Rewriting query..."
python3 "$SCRIPT_DIR/rewrite_chain_query.py" "$QUERY" "$TEMP_DIR/decomposed.sql" 2>&1

echo "Decomposed query:"
cat "$TEMP_DIR/decomposed.sql"
echo ""

# Step 5: Run Multi-Way Band Joins (TIMED)
echo ""
echo "[5/5] Running multi-way band joins..."
START_MWBJ=$(date +%s.%N)
"$PROJECT_DIR/sgx_app" "$TEMP_DIR/decomposed.sql" "$TEMP_DIR/mwbj_data" "$OUTPUT" 2>&1
END_MWBJ=$(date +%s.%N)
MWBJ_TIME=$(echo "$END_MWBJ - $START_MWBJ" | bc)

# Report timing (only execution times)
echo ""
echo "========================================"
echo "         TIMING REPORT"
echo "========================================"
echo "One-Hop execution:        ${ONEHOP_TIME}s"
echo "Multi-Way Band Joins:     ${MWBJ_TIME}s"
TOTAL=$(echo "$ONEHOP_TIME + $MWBJ_TIME" | bc)
echo "----------------------------------------"
echo "Total Pipeline Time:      ${TOTAL}s"
echo "========================================"

# Show output stats
if [ -f "$OUTPUT" ]; then
    OUTPUT_ROWS=$(wc -l < "$OUTPUT")
    echo ""
    echo "Output: $OUTPUT ($((OUTPUT_ROWS - 1)) rows)"
fi
