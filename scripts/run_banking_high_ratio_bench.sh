#!/usr/bin/env bash
# Run banking_onehop benchmarks on high-ratio (1000:1) datasets.
# Runs sequentially and prints a summary table at the end.
#
# Usage: bash scripts/run_banking_high_ratio_bench.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/obligraph/build"
BINARY="$BUILD_DIR/banking_onehop"
DATA_BASE="$PROJECT_ROOT/input/plaintext/banking_high_ratio"
OUT_DIR="$PROJECT_ROOT/output/banking_high_ratio_bench"

SIZES=("100K" "200K" "500K" "1M")

# Build if needed
if [ ! -f "$BINARY" ]; then
    echo "Building banking_onehop..."
    cmake --build "$BUILD_DIR" -j14
fi

mkdir -p "$OUT_DIR"

echo "========================================"
echo " Banking High-Ratio Benchmark (1000:1)"
echo " $(date)"
echo "========================================"
echo ""

declare -A BUILD_MS
declare -A PROBE_MS
declare -A TOTAL_MS
declare -A RESULT_ROWS
declare -A STATUS

for sz in "${SIZES[@]}"; do
    data_dir="$DATA_BASE/banking_${sz}_txn"
    out_csv="$OUT_DIR/result_${sz}.csv"
    log_file="$OUT_DIR/log_${sz}.txt"

    if [ ! -d "$data_dir" ]; then
        echo "[SKIP] $data_dir not found"
        STATUS[$sz]="SKIP"
        continue
    fi

    echo "[RUN] banking_${sz}_txn ..."
    start_time=$SECONDS

    "$BINARY" "$data_dir" "$out_csv" > "$log_file" 2>&1 && STATUS[$sz]="DONE" || STATUS[$sz]="FAIL"

    elapsed=$((SECONDS - start_time))
    echo "  Wall time: ${elapsed}s  Status: ${STATUS[$sz]}"
    echo "  Log: $log_file"

    # Parse timing from output using awk (no pipes)
    BUILD_MS[$sz]=$(awk '/Index build \(offline\):.*ms/{for(i=1;i<=NF;i++) if($(i+1)=="ms") print $i}' "$log_file") || BUILD_MS[$sz]="?"
    PROBE_MS[$sz]=$(awk '/One-hop probe \(online\):.*ms/{for(i=1;i<=NF;i++) if($(i+1)=="ms") print $i}' "$log_file") || PROBE_MS[$sz]="?"
    TOTAL_MS[$sz]=$(awk '/Total \(with I\/O\):.*ms/{for(i=1;i<=NF;i++) if($(i+1)=="ms") print $i}' "$log_file") || TOTAL_MS[$sz]="?"
    RESULT_ROWS[$sz]=$(awk '/^Result:/{print $2}' "$log_file") || RESULT_ROWS[$sz]="?"

    echo ""
done

echo "========================================"
echo " Summary"
echo "========================================"
printf "%-10s  %8s  %10s  %10s  %10s  %s\n" "Dataset" "Status" "Build(ms)" "Probe(ms)" "Total(ms)" "Rows"
printf "%-10s  %8s  %10s  %10s  %10s  %s\n" "-------" "------" "---------" "---------" "---------" "----"
for sz in "${SIZES[@]}"; do
    printf "%-10s  %8s  %10s  %10s  %10s  %s\n" \
        "${sz}" \
        "${STATUS[$sz]:-?}" \
        "${BUILD_MS[$sz]:-?}" \
        "${PROBE_MS[$sz]:-?}" \
        "${TOTAL_MS[$sz]:-?}" \
        "${RESULT_ROWS[$sz]:-?}"
done
echo "========================================"
echo " Logs in: $OUT_DIR/"
echo "========================================"
