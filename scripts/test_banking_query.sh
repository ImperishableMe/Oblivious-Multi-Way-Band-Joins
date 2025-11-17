#!/bin/bash
# Test script for banking dataset queries

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Directories
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="$PROJECT_ROOT/input/plaintext/banking"
QUERY_DIR="$PROJECT_ROOT/input/queries"
OUTPUT_DIR="$PROJECT_ROOT/output/banking"

echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE}Banking Dataset Query Test${NC}"
echo -e "${BLUE}============================================================${NC}"

# Check if data exists
if [ ! -f "$DATA_DIR/account.csv" ] || [ ! -f "$DATA_DIR/txn.csv" ]; then
    echo -e "${RED}Error: Banking dataset not found!${NC}"
    echo -e "${YELLOW}Run: python3 scripts/generate_banking_data.py${NC}"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if executables exist
if [ ! -f "$PROJECT_ROOT/sgx_app" ]; then
    echo -e "${YELLOW}Building sgx_app...${NC}"
    cd "$PROJECT_ROOT"
    make clean
    make
fi

echo ""
echo -e "${GREEN}Running 2-way join (account-txn)${NC}"
echo "Query: SELECT * FROM account, txn WHERE account.account_id = txn.acc_from"
echo ""

time "$PROJECT_ROOT/sgx_app" \
    "$QUERY_DIR/banking_simple.sql" \
    "$DATA_DIR" \
    "$OUTPUT_DIR/banking_output.csv"

# Check output
if [ -f "$OUTPUT_DIR/banking_output.csv" ]; then
    line_count=$(wc -l < "$OUTPUT_DIR/banking_output.csv")
    echo -e "${GREEN}✓ Query completed successfully!${NC}"
    echo "Output: $OUTPUT_DIR/banking_output.csv"
    echo "Result rows: $((line_count - 1))"  # Subtract header
    echo ""
    echo "Sample output (first 5 rows):"
    head -n 6 "$OUTPUT_DIR/banking_output.csv"
else
    echo -e "${RED}✗ Query failed - no output file generated${NC}"
    exit 1
fi

# Check if test_join exists for baseline comparison
if [ -f "$PROJECT_ROOT/test_join" ]; then
    echo ""
    echo -e "${YELLOW}Comparing with SQLite baseline...${NC}"
    echo ""

    if "$PROJECT_ROOT/test_join" "$QUERY_DIR/banking_simple.sql" "$DATA_DIR"; then
        echo -e "${GREEN}✓ Results match SQLite baseline perfectly!${NC}"
    else
        echo -e "${RED}✗ Results do NOT match SQLite baseline${NC}"
        exit 1
    fi
else
    echo ""
    echo -e "${YELLOW}Note: test_join not found - skipping baseline comparison${NC}"
    echo -e "${YELLOW}Build with: make test_join${NC}"
fi

echo ""
echo -e "${BLUE}============================================================${NC}"
echo -e "${GREEN}All tests completed successfully!${NC}"
echo -e "${BLUE}============================================================${NC}"
echo ""
echo "Output file: $OUTPUT_DIR/banking_output.csv"
echo ""
echo "Dataset statistics:"
wc -l "$DATA_DIR"/*.csv
echo ""
echo "Query: Finds all transactions sent FROM each account"
echo "Result: Each row contains account info + transaction info"
