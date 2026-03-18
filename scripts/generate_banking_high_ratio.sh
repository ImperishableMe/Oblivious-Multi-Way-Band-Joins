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

# Account counts chosen so that:
# 1. num_accounts*(num_accounts-1) > num_transactions (unique pair feasibility)
# 2. num_transactions % num_accounts == 0 (exact txn-ratio)
# 3. Fill factor ~40-50% for fast rejection-free generation
declare -A ACCOUNTS
ACCOUNTS[100K]=500
ACCOUNTS[200K]=625
ACCOUNTS[500K]=1000
ACCOUNTS[1M]=1600
ACCOUNTS[5M]=3125
ACCOUNTS[10M]=5000

# Transaction counts (the defining feature of each dataset)
declare -A TXNS
TXNS[100K]=100000
TXNS[200K]=200000
TXNS[500K]=500000
TXNS[1M]=1000000
TXNS[5M]=5000000
TXNS[10M]=10000000

ORDERED_KEYS=("100K" "200K" "500K" "1M" "5M" "10M")

echo "========================================"
echo " Banking High-Ratio Dataset Generator"
echo " Ratio: ${TXN_RATIO}:1 (txn:account)"
echo " $(date)"
echo "========================================"
echo ""

for key in "${ORDERED_KEYS[@]}"; do
    num_accounts=${ACCOUNTS[$key]}
    num_txn=${TXNS[$key]}
    txn_ratio=$((num_txn / num_accounts))
    out_dir="$OUT_BASE/banking_${key}_txn"

    echo "--- Generating ${key} transactions (${num_accounts} accounts, ratio ${txn_ratio}) ---"
    python3 "$GENERATOR" "$num_accounts" "$out_dir" --txn-ratio "$txn_ratio" --quiet

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
