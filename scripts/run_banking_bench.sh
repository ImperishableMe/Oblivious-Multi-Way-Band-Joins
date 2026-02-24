#!/usr/bin/env bash
# Run sgx_app on all 6 banking datasets in parallel, with live status tracking.
# Usage: bash scripts/run_banking_bench.sh
#
# Output goes to output/banking_bench/:
#   stdout_<size>.log, stderr_<size>.log, result_<size>.csv

set -euo pipefail

QUERY="input/queries/banking_chain4_filtered.sql"
DATA_BASE="input/plaintext/banking_scaled"
OUT_DIR="output/banking_bench"
SIZES=("10K" "50K" "100K" "200K" "500K" "1M")

mkdir -p "$OUT_DIR"

declare -A PIDS
declare -A START_TIMES
declare -A FINISH_STATUS   # cached: "DONE" or "FAIL" once reaped
declare -A FINISH_ELAPSED  # cached elapsed time at completion

echo "========================================"
echo " Banking Benchmark — chain4_filtered"
echo " $(date)"
echo "========================================"
echo ""

# Launch all jobs
for sz in "${SIZES[@]}"; do
    ./sgx_app "$QUERY" "$DATA_BASE/banking_$sz" "$OUT_DIR/result_$sz.csv" \
        > "$OUT_DIR/stdout_$sz.log" \
        2> "$OUT_DIR/stderr_$sz.log" &
    PIDS[$sz]=$!
    START_TIMES[$sz]=$SECONDS
    echo "[LAUNCHED] banking_$sz  (PID ${PIDS[$sz]})"
done

echo ""
echo "All 6 jobs launched. Polling every 10s..."
echo ""

# Poll until all done
while true; do
    all_done=true
    printf "\033[2K\r"  # clear line
    echo "--- Status at $(date +%H:%M:%S) ---"

    for sz in "${SIZES[@]}"; do
        pid=${PIDS[$sz]}

        # Already reaped — use cached result
        if [[ -n "${FINISH_STATUS[$sz]:-}" ]]; then
            mins=$(( FINISH_ELAPSED[$sz] / 60 ))
            secs=$(( FINISH_ELAPSED[$sz] % 60 ))
            printf "  %-12s  %-6s    %3dm %02ds\n" "banking_$sz" "${FINISH_STATUS[$sz]}" "$mins" "$secs"
            continue
        fi

        elapsed=$(( SECONDS - START_TIMES[$sz] ))
        mins=$(( elapsed / 60 ))
        secs=$(( elapsed % 60 ))

        if kill -0 "$pid" 2>/dev/null; then
            printf "  %-12s  RUNNING   %3dm %02ds   (PID %d)\n" "banking_$sz" "$mins" "$secs" "$pid"
            all_done=false
        else
            # Reap once and cache
            wait "$pid" 2>/dev/null && FINISH_STATUS[$sz]="DONE" || FINISH_STATUS[$sz]="FAIL"
            FINISH_ELAPSED[$sz]=$elapsed
            printf "  %-12s  %-6s    %3dm %02ds\n" "banking_$sz" "${FINISH_STATUS[$sz]}" "$mins" "$secs"
        fi
    done

    if $all_done; then
        break
    fi

    echo ""
    sleep 10
done

TOTAL=$SECONDS
echo ""
echo "========================================"
echo " All jobs finished in $((TOTAL/60))m $((TOTAL%60))s"
echo " Results in: $OUT_DIR/"
echo "========================================"
