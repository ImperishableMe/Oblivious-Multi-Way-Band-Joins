#!/bin/bash
# Scaling Benchmark Script
#
# Runs benchmark_decomposition.sh across multiple dataset sizes and outputs
# results to a CSV file for plotting.
#
# Usage: ./scripts/benchmark_scaling.sh <query.sql> [num_runs]
#
# Example:
#   ./scripts/benchmark_scaling.sh input/queries/banking_chain4_filtered.sql 3

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <query.sql> [num_runs]"
    echo ""
    echo "Arguments:"
    echo "  query.sql   Input SQL query file"
    echo "  num_runs    Number of runs per scale (default: 3)"
    echo ""
    echo "Example:"
    echo "  $0 input/queries/banking_chain4_filtered.sql 3"
    exit 1
fi

QUERY=$1
NUM_RUNS=${2:-3}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Scale factors to test
SCALES=(1000 2000 5000 10000 20000 50000)

# Extract query name for output file
QUERY_NAME=$(basename "$QUERY" .sql)

# Output file
mkdir -p "$PROJECT_DIR/output"
OUTPUT_CSV="$PROJECT_DIR/output/scaling_results_${QUERY_NAME}.csv"

echo "=============================================="
echo "  SCALING BENCHMARK"
echo "=============================================="
echo "Query:     $QUERY_NAME"
echo "Scales:    ${SCALES[*]}"
echo "Runs:      $NUM_RUNS"
echo "Output:    $OUTPUT_CSV"
echo ""

# Write CSV header
echo "accounts,transactions,non_decomposed,decomposed,onehop,mwbj,speedup,rows" > "$OUTPUT_CSV"

# Run benchmark for each scale
for SCALE in "${SCALES[@]}"; do
    echo "----------------------------------------------"
    echo "Scale: $SCALE accounts"
    echo "----------------------------------------------"

    DATA_DIR="$PROJECT_DIR/input/plaintext/banking_${SCALE}"

    # Generate data if it doesn't exist
    if [ ! -d "$DATA_DIR" ]; then
        echo "Generating dataset..."
        python3 "$SCRIPT_DIR/generate_banking_scaled.py" "$SCALE" "$DATA_DIR"
    else
        echo "Using existing dataset at $DATA_DIR"
    fi

    # Calculate expected transactions
    TRANSACTIONS=$((SCALE * 5))

    # Run benchmark and capture output
    echo "Running benchmark..."
    BENCHMARK_OUTPUT=$("$SCRIPT_DIR/benchmark_decomposition.sh" "$QUERY" "$DATA_DIR" "$NUM_RUNS" 2>&1)

    # Parse results from output
    # Extract "Non-Decomposed" average time
    NON_DECOMPOSED=$(echo "$BENCHMARK_OUTPUT" | grep -oP 'Non-Decomposed\s+\K[0-9]+\.[0-9]+')

    # Extract "Decomposed" average time (use negative lookbehind to avoid matching "Non-Decomposed")
    DECOMPOSED=$(echo "$BENCHMARK_OUTPUT" | grep -oP '(?<!Non-)Decomposed\s+\K[0-9]+\.[0-9]+')

    # Extract One-hop average
    ONEHOP=$(echo "$BENCHMARK_OUTPUT" | grep -oP 'One-hop:\s+\K[0-9]+\.[0-9]+')

    # Extract MWBJ average
    MWBJ=$(echo "$BENCHMARK_OUTPUT" | grep -oP 'MWBJ:\s+\K[0-9]+\.[0-9]+')

    # Extract Speedup
    SPEEDUP=$(echo "$BENCHMARK_OUTPUT" | grep -oP 'Speedup: \K[0-9]+\.[0-9]+')

    # Extract row count (from first occurrence)
    ROWS=$(echo "$BENCHMARK_OUTPUT" | grep -oP '\(\K\d+(?= rows\))' | head -1)

    # Default values if parsing fails
    NON_DECOMPOSED=${NON_DECOMPOSED:-0}
    DECOMPOSED=${DECOMPOSED:-0}
    ONEHOP=${ONEHOP:-0}
    MWBJ=${MWBJ:-0}
    SPEEDUP=${SPEEDUP:-0}
    ROWS=${ROWS:-0}

    # Print summary for this scale
    echo "  Non-Decomposed: ${NON_DECOMPOSED}s"
    echo "  Decomposed:     ${DECOMPOSED}s (onehop: ${ONEHOP}s, mwbj: ${MWBJ}s)"
    echo "  Speedup:        ${SPEEDUP}x"
    echo "  Rows:           $ROWS"
    echo ""

    # Append to CSV
    echo "${SCALE},${TRANSACTIONS},${NON_DECOMPOSED},${DECOMPOSED},${ONEHOP},${MWBJ},${SPEEDUP},${ROWS}" >> "$OUTPUT_CSV"
done

echo "=============================================="
echo "  SCALING BENCHMARK COMPLETE"
echo "=============================================="
echo "Results saved to: $OUTPUT_CSV"
echo ""
echo "To generate plot:"
echo "  python3 $SCRIPT_DIR/plot_scaling_benchmark.py $OUTPUT_CSV output/scaling_${QUERY_NAME}.png"
echo ""
