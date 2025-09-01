#!/bin/bash
# Run complete encrypted dataset verification workflow

set -e  # Exit on error

echo "=============================================================="
echo "    ENCRYPTED DATASET OBLIVIOUS VERIFICATION WORKFLOW"
echo "=============================================================="
echo ""

# Base paths
BASE_PATH="/home/r33wei/omwj/memory_const"
TEST_PATH="$BASE_PATH/test_access_patterns"

# Step 1: Encrypt datasets
echo "[Step 1/3] Encrypting datasets..."
bash "$TEST_PATH/scripts/encrypt_all_datasets.sh"

# Step 2: Collect traces with encrypted data
echo ""
echo "[Step 2/3] Collecting memory traces with encrypted datasets..."
echo "NOTE: This may take several minutes per query due to SGX overhead..."
cd "$TEST_PATH"
python3 scripts/collect_traces.py --all --encrypted

# Step 3: Compare traces
echo ""
echo "[Step 3/3] Comparing memory access patterns..."
python3 scripts/compare_traces.py --all --encrypted --html

# Display results
echo ""
echo "=============================================================="
echo "                   VERIFICATION COMPLETE"
echo "=============================================================="
echo ""
echo "Reports available at:"
echo "  - JSON: $TEST_PATH/reports/trace_comparison_report.json"
echo "  - HTML: $TEST_PATH/reports/summary_report.html"
echo ""
echo "Trace files saved at:"
echo "  - Raw:       $TEST_PATH/traces/raw/"
echo "  - Processed: $TEST_PATH/traces/processed/"