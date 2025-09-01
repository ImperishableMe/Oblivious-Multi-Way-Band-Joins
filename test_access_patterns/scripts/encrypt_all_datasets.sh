#!/bin/bash
# Encrypt all test datasets using the existing encrypt_tables binary

set -e  # Exit on error

# Change to the SGX app directory where encrypt_tables is located
cd /home/r33wei/omwj/memory_const/impl/src

# Base path for test datasets
TEST_PATH="/home/r33wei/omwj/memory_const/test_access_patterns"

echo "=========================================="
echo "Encrypting all test datasets"
echo "=========================================="

# Encrypt dataset_A
echo ""
echo "Encrypting dataset_A..."
for query in tb1 tb2 tm1 tm2 tm3; do
    echo "  Encrypting dataset_A/$query..."
    ./encrypt_tables "$TEST_PATH/dataset_A/$query" "$TEST_PATH/encrypted_dataset_A/$query"
done

# Encrypt dataset_B
echo ""
echo "Encrypting dataset_B..."
for query in tb1 tb2 tm1 tm2 tm3; do
    echo "  Encrypting dataset_B/$query..."
    ./encrypt_tables "$TEST_PATH/dataset_B/$query" "$TEST_PATH/encrypted_dataset_B/$query"
done

echo ""
echo "=========================================="
echo "✓ All datasets encrypted successfully"
echo "=========================================="
echo ""
echo "Encrypted datasets are located at:"
echo "  - $TEST_PATH/encrypted_dataset_A/"
echo "  - $TEST_PATH/encrypted_dataset_B/"