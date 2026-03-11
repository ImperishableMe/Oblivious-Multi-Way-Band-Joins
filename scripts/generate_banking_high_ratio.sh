#!/usr/bin/env bash
# Generate banking datasets with 1000:1 transaction-to-account ratio.
# Produces 4 scales: 100K, 200K, 500K, 1M transactions.
#
# Usage: bash scripts/generate_banking_high_ratio.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GENERATOR="$SCRIPT_DIR/generate_banking_scaled.py"
OUT_BASE="$PROJECT_ROOT/input/plaintext/banking_high_ratio"
TXN_RATIO=1000

declare -A SCALES
SCALES[100K]=100
SCALES[200K]=200
SCALES[500K]=500
SCALES[1M]=1000

ORDERED_KEYS=("100K" "200K" "500K" "1M")

echo "========================================"
echo " Banking High-Ratio Dataset Generator"
echo " Ratio: ${TXN_RATIO}:1 (txn:account)"
echo " $(date)"
echo "========================================"
echo ""

for key in "${ORDERED_KEYS[@]}"; do
    num_accounts=${SCALES[$key]}
    num_txn=$((num_accounts * TXN_RATIO))
    out_dir="$OUT_BASE/banking_${key}_txn"

    echo "--- Generating ${key} transactions (${num_accounts} accounts x ${TXN_RATIO}) ---"
    python3 "$GENERATOR" "$num_accounts" "$out_dir" --txn-ratio "$TXN_RATIO" --quiet

    # Verify row counts (header + data rows + sentinel = expected + 2)
    expected_acct=$((num_accounts + 2))
    expected_txn=$((num_txn + 2))

    actual_acct=$(wc -l < "$out_dir/account.csv")
    actual_txn=$(wc -l < "$out_dir/txn.csv")

    if [ "$actual_acct" -ne "$expected_acct" ]; then
        echo "  ERROR: account.csv has $actual_acct lines, expected $expected_acct"
        exit 1
    fi
    if [ "$actual_txn" -ne "$expected_txn" ]; then
        echo "  ERROR: txn.csv has $actual_txn lines, expected $expected_txn"
        exit 1
    fi
    echo "  Verified: account.csv=$actual_acct lines, txn.csv=$actual_txn lines"
    echo ""
done

echo "========================================"
echo " All datasets generated in: $OUT_BASE/"
echo "========================================"
