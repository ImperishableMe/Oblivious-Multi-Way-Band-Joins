#!/usr/bin/env python3
"""
Generate a small banking dataset for testing complex queries.
Creates 100 accounts and 200 transactions.
"""

import csv
import random
import os

# Configuration
NUM_ACCOUNTS = 100
NUM_TRANSACTIONS = 200
NUM_OWNERS = 50
OUTPUT_DIR = "input/plaintext/banking_small"

# Data bounds from enclave_types.h
MIN_VALUE = -1_073_741_820
MAX_VALUE = 1_073_741_820
SENTINEL = -10000

def generate_dataset(output_dir):
    """Generate small banking dataset."""
    os.makedirs(output_dir, exist_ok=True)

    # Set seed for reproducibility
    random.seed(42)

    # Generate owners
    print(f"Generating {NUM_OWNERS} owners...")
    owner_file = os.path.join(output_dir, "owner.csv")
    with open(owner_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['ow_id', 'name_placeholder'])
        for i in range(1, NUM_OWNERS + 1):
            writer.writerow([i, i])
        writer.writerow([SENTINEL, SENTINEL])

    # Generate accounts
    print(f"Generating {NUM_ACCOUNTS} accounts...")
    account_file = os.path.join(output_dir, "account.csv")
    with open(account_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['account_id', 'balance', 'owner_id'])
        for i in range(1, NUM_ACCOUNTS + 1):
            balance = random.randint(1000, 1_000_000)
            owner_id = random.randint(1, NUM_OWNERS)
            writer.writerow([i, balance, owner_id])
        writer.writerow([SENTINEL, SENTINEL, SENTINEL])

    # Generate transactions
    print(f"Generating {NUM_TRANSACTIONS} transactions...")
    txn_file = os.path.join(output_dir, "txn.csv")
    with open(txn_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['acc_from', 'acc_to', 'amount', 'txn_time'])
        for _ in range(NUM_TRANSACTIONS):
            acc_from = random.randint(1, NUM_ACCOUNTS)
            acc_to = random.randint(1, NUM_ACCOUNTS)
            # Ensure acc_from != acc_to
            while acc_to == acc_from:
                acc_to = random.randint(1, NUM_ACCOUNTS)
            amount = random.randint(100, 100_000)
            txn_time = random.randint(1, 500_000)
            writer.writerow([acc_from, acc_to, amount, txn_time])
        writer.writerow([SENTINEL, SENTINEL, SENTINEL, SENTINEL])

    print(f"\nDataset generated in: {output_dir}")
    print(f"  - {NUM_OWNERS} owners")
    print(f"  - {NUM_ACCOUNTS} accounts")
    print(f"  - {NUM_TRANSACTIONS} transactions")

if __name__ == "__main__":
    # Get project root (script is in scripts/ subdirectory)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_path = os.path.join(project_root, OUTPUT_DIR)

    generate_dataset(output_path)
