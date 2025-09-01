#!/usr/bin/env python3

import sqlite3
import os
import sys
import csv
from pathlib import Path

# Configuration
BASE_DIR = "/home/r33wei/omwj/memory_const"
QUERY_DIR = f"{BASE_DIR}/input/queries"
DATA_BASE = f"{BASE_DIR}/input/plaintext"
DATASETS = ["data_0_001", "data_0_01", "data_0_1"]
QUERIES = ["tpch_tb1", "tpch_tb2", "tpch_tm1", "tpch_tm2", "tpch_tm3"]

def load_csv_to_sqlite(conn, csv_path, table_name):
    """Load a CSV file into SQLite table"""
    with open(csv_path, 'r') as f:
        reader = csv.reader(f)
        headers = next(reader)
        
        # Create table with all columns as TEXT
        cols = ', '.join([f'"{h}" TEXT' for h in headers])
        conn.execute(f'DROP TABLE IF EXISTS {table_name}')
        conn.execute(f'CREATE TABLE {table_name} ({cols})')
        
        # Insert data
        placeholders = ', '.join(['?' for _ in headers])
        for row in reader:
            conn.execute(f'INSERT INTO {table_name} VALUES ({placeholders})', row)
    conn.commit()

def convert_to_count_query(query):
    """Convert SELECT * to SELECT COUNT(*)"""
    # Simple replacement for our TPC-H queries
    return query.replace("SELECT *", "SELECT COUNT(*)")

def get_row_count(dataset, query_file):
    """Get row count for a query on a dataset"""
    data_path = f"{DATA_BASE}/{dataset}"
    
    if not os.path.exists(data_path):
        return None
    
    # Read query
    with open(query_file, 'r') as f:
        original_query = f.read()
    
    # Convert to COUNT query
    count_query = convert_to_count_query(original_query)
    
    # Create temporary database
    conn = sqlite3.connect(':memory:')
    
    try:
        # Load all CSV files in dataset
        for csv_file in Path(data_path).glob('*.csv'):
            table_name = csv_file.stem
            load_csv_to_sqlite(conn, csv_file, table_name)
        
        # Execute count query
        cursor = conn.execute(count_query)
        result = cursor.fetchone()[0]
        return result
    except Exception as e:
        print(f"Error for {dataset}/{os.path.basename(query_file)}: {e}", file=sys.stderr)
        return None
    finally:
        conn.close()

def main():
    # Open output file
    output_file = f"{BASE_DIR}/output/query_counts_summary.txt"
    
    # Function to print to both console and file
    def print_both(text=""):
        print(text)
        with open(output_file, 'a') as f:
            f.write(text + '\n')
    
    # Clear file first
    with open(output_file, 'w') as f:
        f.write("")
    
    print_both("=" * 60)
    print_both("TPC-H Query Result Counts (using COUNT(*))")
    print_both("=" * 60)
    print_both()
    print_both(f"Datasets: {' '.join(DATASETS)}")
    print_both()
    
    # Print header
    header = f"{'Query':<12}"
    for dataset in DATASETS:
        header += f"{dataset:<15}"
    print_both(header)
    
    separator = f"{'-'*12:<12}"
    for dataset in DATASETS:
        separator += f"{'-'*14:<15}"
    print_both(separator)
    
    # Store counts for growth factor calculation
    counts = {}
    
    # Process each query
    for query in QUERIES:
        query_file = f"{QUERY_DIR}/{query}.sql"
        
        if not os.path.exists(query_file):
            row = f"{query:<12}"
            for dataset in DATASETS:
                row += f"{'N/A':<15}"
            print_both(row)
            continue
        
        row = f"{query:<12}"
        
        for dataset in DATASETS:
            count = get_row_count(dataset, query_file)
            counts[f"{query}_{dataset}"] = count
            
            if count is not None:
                row += f"{count:<15,}"
            else:
                row += f"{'Error':<15}"
        print_both(row)
    
    print_both()
    print_both("=" * 60)
    print_both("Growth Factors (relative to data_0_001)")
    print_both("=" * 60)
    print_both()
    
    # Calculate growth factors
    for query in QUERIES:
        base_count = counts.get(f"{query}_data_0_001")
        if base_count and base_count > 0:
            row = f"{query:<12}"
            for dataset in DATASETS:
                count = counts.get(f"{query}_{dataset}")
                if count:
                    factor = count / base_count
                    row += f"{dataset}: {factor:.1f}x  "
            print_both(row)
    
    print_both()
    print_both("Note: Using SELECT COUNT(*) for efficiency with large datasets")
    print_both(f"Results saved to: {output_file}")

if __name__ == "__main__":
    main()