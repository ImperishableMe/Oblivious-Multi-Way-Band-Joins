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

# Step 2: Run one-hop (extract timing from program output)
echo ""
echo "[2/5] Running one-hop join..."
ONEHOP_OUTPUT=$("$PROJECT_DIR/obligraph/build/banking_onehop" "$TEMP_DIR/obligraph_data" "$TEMP_DIR/hop.csv" 2>&1)
echo "$ONEHOP_OUTPUT"

# Extract "One-hop execution: X ms" from output and convert to seconds
ONEHOP_MS=$(echo "$ONEHOP_OUTPUT" | grep -oP 'One-hop execution: \K[0-9]+')
if [ -z "$ONEHOP_MS" ]; then
    echo "Warning: Could not extract one-hop timing from output"
    ONEHOP_TIME="0"
else
    ONEHOP_TIME=$(echo "scale=6; $ONEHOP_MS / 1000" | bc)
fi

# Step 3: Prepare hop result for Multi-Way Band Joins (NOT timed)
echo ""
echo "[3/5] Preparing intermediate data..."
mkdir -p "$TEMP_DIR/mwbj_data"
cp "$TEMP_DIR/hop.csv" "$TEMP_DIR/mwbj_data/"
# Also copy txn.csv for optimized queries that use plain txn tables
cp "$DATA_DIR/txn.csv" "$TEMP_DIR/mwbj_data/"

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

# Step 5: Run Multi-Way Band Joins (extract timing from program output)
echo ""
echo "[5/5] Running multi-way band joins..."
MWBJ_OUTPUT=$("$PROJECT_DIR/sgx_app" "$TEMP_DIR/decomposed.sql" "$TEMP_DIR/mwbj_data" "$OUTPUT" 2>&1)
echo "$MWBJ_OUTPUT"

# Extract "Total=X.XXXXXX" from PHASE_TIMING line (time is in seconds)
MWBJ_TIME=$(echo "$MWBJ_OUTPUT" | grep -oP 'PHASE_TIMING:.*Total=\K[0-9]+\.[0-9]+')
if [ -z "$MWBJ_TIME" ]; then
    echo "Warning: Could not extract MWBJ timing from output"
    MWBJ_TIME="0"
fi

# Report timing (query execution times only, excludes I/O)
echo ""
echo "========================================"
echo "    TIMING REPORT (Query Execution)"
echo "========================================"
echo "One-Hop query time:       ${ONEHOP_TIME}s"
echo "Multi-Way Band Joins:     ${MWBJ_TIME}s"
TOTAL=$(echo "$ONEHOP_TIME + $MWBJ_TIME" | bc)
echo "----------------------------------------"
echo "Total Query Time:         ${TOTAL}s"
echo "========================================"

# Show output stats
if [ -f "$OUTPUT" ]; then
    OUTPUT_ROWS=$(wc -l < "$OUTPUT")
    echo ""
    echo "Output: $OUTPUT ($((OUTPUT_ROWS - 1)) rows)"
fi
