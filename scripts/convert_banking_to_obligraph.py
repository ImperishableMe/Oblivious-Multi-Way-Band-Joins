#!/usr/bin/env python3
"""
Convert banking dataset from Multi-Way Band Joins format to ObliGraph format.

Multi-Way Band Joins format:
- Comma-delimited
- No type row
- account.csv: account_id,balance,owner_id
- txn.csv: acc_from,acc_to,amount,txn_time

ObliGraph format:
- Pipe-delimited
- Type row after header
- Node tables need 'id' column
- Edge tables: <src>_<edge>_<dest>.csv with <src>1Id, <src>2Id for self-referential
"""

import os
import sys
import csv


def convert_account_csv(input_path: str, output_path: str) -> None:
    """Convert account.csv to ObliGraph format."""
    with open(input_path, 'r') as infile:
        reader = csv.DictReader(infile)
        rows = list(reader)

    with open(output_path, 'w') as outfile:
        # Header: rename account_id to id
        outfile.write("id|balance|owner_id\n")
        # Type row
        outfile.write("int64|int32|int32\n")
        # Data rows
        for row in rows:
            outfile.write(f"{row['account_id']}|{row['balance']}|{row['owner_id']}\n")

    print(f"Converted account.csv: {len(rows)} rows")


def convert_txn_csv(input_path: str, output_path: str) -> None:
    """Convert txn.csv to ObliGraph edge format (account_txn_account.csv)."""
    with open(input_path, 'r') as infile:
        reader = csv.DictReader(infile)
        rows = list(reader)

    with open(output_path, 'w') as outfile:
        # Header: use account1Id and account2Id for self-referential edge
        outfile.write("account1Id|account2Id|amount|txn_time\n")
        # Type row
        outfile.write("int64|int64|int32|int32\n")
        # Data rows
        for row in rows:
            outfile.write(f"{row['acc_from']}|{row['acc_to']}|{row['amount']}|{row['txn_time']}\n")

    print(f"Converted txn.csv -> account_txn_account.csv: {len(rows)} rows")


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_dir> <output_dir>")
        print("  input_dir: Directory containing account.csv and txn.csv")
        print("  output_dir: Directory to write ObliGraph-formatted files")
        sys.exit(1)

    input_dir = sys.argv[1]
    output_dir = sys.argv[2]

    # Validate input files exist
    account_input = os.path.join(input_dir, "account.csv")
    txn_input = os.path.join(input_dir, "txn.csv")

    if not os.path.exists(account_input):
        print(f"Error: {account_input} not found")
        sys.exit(1)
    if not os.path.exists(txn_input):
        print(f"Error: {txn_input} not found")
        sys.exit(1)

    # Create output directory if needed
    os.makedirs(output_dir, exist_ok=True)

    # Convert files
    convert_account_csv(account_input, os.path.join(output_dir, "account.csv"))
    convert_txn_csv(txn_input, os.path.join(output_dir, "account_txn_account.csv"))

    print(f"Conversion complete. Output in: {output_dir}")


if __name__ == "__main__":
    main()
