#!/usr/bin/env python3
"""
generate_banking_test_data.py

Generates a synthetic banking dataset for testing.

Usage:
    python3 scripts/generate_banking_test_data.py <output_dir> [--accounts N] [--txns N] [--seed N]

Defaults: 200 accounts, 1000 transactions, seed=42.

Output files:
    <output_dir>/account.csv  — account_id, balance, owner_id
    <output_dir>/txn.csv      — txn_id, acc_from, acc_to, amount, txn_time

All values are integers within the system's valid range [-1,073,741,820, 1,073,741,820].
All acc_from and acc_to reference valid account IDs (no dangling edges).
No self-loops (acc_from != acc_to).
txn_id is unique and sequential starting from 1.
"""

import argparse
import csv
import os
import random


def generate(output_dir: str, num_accounts: int, num_txns: int, seed: int) -> None:
    random.seed(seed)
    os.makedirs(output_dir, exist_ok=True)

    account_ids = list(range(1, num_accounts + 1))

    # Write account.csv
    # Use lineterminator="\n" to produce Unix line endings. The C++ CSV parser uses
    # getline() which strips \n but not \r, so \r\n endings would embed \r into column names.
    account_path = os.path.join(output_dir, "account.csv")
    with open(account_path, "w", newline="") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["account_id", "balance", "owner_id"])
        for aid in account_ids:
            balance = random.randint(1_000, 1_000_000)
            owner_id = random.randint(1, 100)
            writer.writerow([aid, balance, owner_id])

    # Write txn.csv
    txn_path = os.path.join(output_dir, "txn.csv")
    with open(txn_path, "w", newline="") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["txn_id", "acc_from", "acc_to", "amount", "txn_time"])
        for txn_id in range(1, num_txns + 1):
            acc_from = random.choice(account_ids)
            # Ensure acc_to != acc_from (no self-loops)
            acc_to = random.choice(account_ids)
            while acc_to == acc_from:
                acc_to = random.choice(account_ids)
            amount = random.randint(1, 100_000)
            txn_time = random.randint(1, 1_000_000)
            writer.writerow([txn_id, acc_from, acc_to, amount, txn_time])

    print(f"Generated {num_accounts} accounts and {num_txns} transactions in {output_dir}/")
    print(f"  {account_path}")
    print(f"  {txn_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate synthetic banking test data")
    parser.add_argument("output_dir", help="Directory to write account.csv and txn.csv")
    parser.add_argument("--accounts", type=int, default=200, help="Number of accounts (default: 200)")
    parser.add_argument("--txns", type=int, default=1000, help="Number of transactions (default: 1000)")
    parser.add_argument("--seed", type=int, default=42, help="Random seed (default: 42)")
    args = parser.parse_args()

    generate(args.output_dir, args.accounts, args.txns, args.seed)


if __name__ == "__main__":
    main()
