#!/bin/bash
# Quick test script for TB1 with encrypted datasets

set -e  # Exit on error

echo "============================================"
echo "Testing TB1 with Encrypted Datasets"
echo "============================================"
echo ""

# Base paths
BASE_PATH="/home/r33wei/omwj/memory_const"
TEST_PATH="$BASE_PATH/test_access_patterns"
SGX_PATH="$BASE_PATH/impl/src"

# Step 1: Encrypt just TB1 datasets
echo "[1/3] Encrypting TB1 datasets..."
cd "$SGX_PATH"
./encrypt_tables "$TEST_PATH/dataset_A/tb1" "$TEST_PATH/encrypted_dataset_A/tb1"
./encrypt_tables "$TEST_PATH/dataset_B/tb1" "$TEST_PATH/encrypted_dataset_B/tb1"
echo "✓ TB1 datasets encrypted"

# Step 2: Collect traces for TB1
echo ""
echo "[2/3] Collecting traces for TB1..."
cd "$TEST_PATH"
python3 scripts/collect_traces.py --query tb1 --dataset dataset_A --encrypted
python3 scripts/collect_traces.py --query tb1 --dataset dataset_B --encrypted
echo "✓ Traces collected"

# Step 3: Compare traces
echo ""
echo "[3/3] Comparing TB1 traces..."
python3 scripts/compare_traces.py --query tb1 --encrypted

echo ""
echo "============================================"
echo "Test complete. Check output above for results."