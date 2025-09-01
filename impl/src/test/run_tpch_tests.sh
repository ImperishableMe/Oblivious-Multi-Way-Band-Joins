#!/bin/bash

# Run all TPCH queries with data_0_001 dataset
QUERY_DIR="/home/r33wei/omwj/memory_const_public/input/queries"
DATA_DIR="/home/r33wei/omwj/memory_const_public/input/encrypted/data_0_001"
TEST_PROG="./test/test_join"

echo "=== Running TPCH Tests ==="
echo "Data: data_0_001"
echo ""

# Process each SQL file
for query_file in $QUERY_DIR/*.sql; do
    if [ -f "$query_file" ]; then
        query_name=$(basename "$query_file" .sql)
        echo "----------------------------------------"
        echo "Running: $query_name"
        echo "----------------------------------------"
        
        # Run test_join
        $TEST_PROG "$query_file" "$DATA_DIR" 2>&1 | tail -15
        
        echo ""
    fi
done

echo "=== All TPCH Tests Complete ==="
echo ""
echo "Summary files written to: /home/r33wei/omwj/memory_const_public/output/"
ls -la /home/r33wei/omwj/memory_const_public/output/*.txt 2>/dev/null