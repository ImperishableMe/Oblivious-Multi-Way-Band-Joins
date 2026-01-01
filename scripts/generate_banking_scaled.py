#!/usr/bin/env python3
"""
Banking Dataset Generator for Oblivious Multi-Way Join Testing

Generates realistic banking data with parameterized sizes:
- Owner table: num_accounts / 5 owners
- Account table: num_accounts accounts with balances and owner IDs
- Transaction table: 5 * num_accounts transactions between accounts

Uses Zipfian distribution for transaction counts to create realistic variance
(few very active accounts, most moderately active).

Usage: python3 generate_banking_scaled.py <num_accounts> <output_dir>

Example:
    python3 scripts/generate_banking_scaled.py 5000 input/plaintext/banking_5000
    python3 scripts/generate_banking_scaled.py 10000 input/plaintext/banking
"""

import argparse
import csv
import random
from collections import defaultdict
from pathlib import Path

# System bounds from enclave_types.h
JOIN_ATTR_MIN = -1_073_741_820
JOIN_ATTR_MAX = 1_073_741_820

# Data ranges
MIN_BALANCE = 0
MAX_BALANCE = 1_000_000
MIN_AMOUNT = 1
MAX_AMOUNT = 100_000
MIN_TIMESTAMP = 1
MAX_TIMESTAMP = 365000

# Sentinel value for last row
SENTINEL = -10000


def zipfian_choice(items, alpha=1.5):
    """Select an item using Zipfian distribution."""
    n = len(items)
    weights = [1.0 / pow(i + 1, alpha) for i in range(n)]
    total_weight = sum(weights)

    rand_val = random.uniform(0, total_weight)
    cumulative = 0.0
    for i, weight in enumerate(weights):
        cumulative += weight
        if rand_val <= cumulative:
            return items[i]
    return items[-1]


def generate_owners(num_owners):
    """Generate owner table."""
    owners = []
    for owner_id in range(1, num_owners + 1):
        owners.append({
            'ow_id': owner_id,
            'name_placeholder': owner_id
        })
    return owners


def generate_accounts(num_accounts, num_owners):
    """Generate account table with realistic distribution."""
    accounts = []
    accounts_per_owner = defaultdict(int)

    for account_id in range(1, num_accounts + 1):
        rand_val = random.random()
        if rand_val < 0.7:
            owner_id = random.randint(1, num_owners)
        else:
            owner_id = random.randint(1, max(1, num_owners // 5))

        balance = random.randint(MIN_BALANCE, MAX_BALANCE)

        accounts.append({
            'account_id': account_id,
            'balance': balance,
            'owner_id': owner_id
        })
        accounts_per_owner[owner_id] += 1

    print(f"Generated {len(accounts)} accounts")
    print(f"Owners with accounts: {len(accounts_per_owner)}")
    print(f"Max accounts per owner: {max(accounts_per_owner.values())}")
    print(f"Avg accounts per owner: {sum(accounts_per_owner.values()) / len(accounts_per_owner):.2f}")

    return accounts


def generate_transactions(accounts, num_transactions):
    """Generate transaction table with Zipfian distribution."""
    transactions = []
    account_ids = [acc['account_id'] for acc in accounts]
    txn_counts = defaultdict(int)

    for _ in range(num_transactions):
        acc_from = zipfian_choice(account_ids, alpha=1.5)

        acc_to = acc_from
        while acc_to == acc_from:
            acc_to = random.choice(account_ids)

        amount = random.randint(MIN_AMOUNT, MAX_AMOUNT)
        timestamp = random.randint(MIN_TIMESTAMP, MAX_TIMESTAMP)

        transactions.append({
            'acc_from': acc_from,
            'acc_to': acc_to,
            'amount': amount,
            'txn_time': timestamp
        })
        txn_counts[acc_from] += 1

    txn_list = sorted(txn_counts.values(), reverse=True)
    print(f"\nGenerated {len(transactions)} transactions")
    print(f"Accounts with outgoing transactions: {len(txn_counts)}")
    print(f"Top 10 most active accounts: {txn_list[:10]}")
    print(f"Median transactions per account: {txn_list[len(txn_list)//2] if txn_list else 0}")
    print(f"Avg transactions per active account: {sum(txn_list) / len(txn_list):.2f}")
    mean = sum(txn_list) / len(txn_list) if txn_list else 0
    variance = sum((x - mean) ** 2 for x in txn_list) / len(txn_list) if txn_list else 0
    print(f"Variance in transaction counts: {variance:.2f}")

    return transactions


def write_csv(output_dir, filename, data, fieldnames):
    """Write data to CSV file with sentinel row."""
    filepath = output_dir / filename

    with open(filepath, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(data)

        # Add sentinel row
        sentinel_row = {field: SENTINEL for field in fieldnames}
        writer.writerow(sentinel_row)


def validate_data(accounts, transactions):
    """Validate foreign key constraints and data bounds."""
    account_ids = {acc['account_id'] for acc in accounts}

    for txn in transactions:
        if txn['acc_from'] not in account_ids or txn['acc_to'] not in account_ids:
            raise ValueError("Foreign key validation failed!")

    for acc in accounts:
        if not (JOIN_ATTR_MIN <= acc['balance'] <= JOIN_ATTR_MAX):
            raise ValueError(f"Balance out of range: {acc['balance']}")

    for txn in transactions:
        if not (JOIN_ATTR_MIN <= txn['amount'] <= JOIN_ATTR_MAX):
            raise ValueError(f"Amount out of range: {txn['amount']}")


def main():
    parser = argparse.ArgumentParser(
        description='Banking Dataset Generator for Oblivious Multi-Way Join Testing'
    )
    parser.add_argument('num_accounts', type=int, help='Number of accounts to generate')
    parser.add_argument('output_dir', type=Path, help='Output directory path')
    parser.add_argument('--seed', type=int, default=None,
                        help='Random seed (default: 42 + num_accounts)')
    args = parser.parse_args()

    num_accounts = args.num_accounts
    output_dir = args.output_dir

    # Calculate dependent sizes (5:1 ratio for transactions, 1:5 ratio for owners)
    num_transactions = 5 * num_accounts
    num_owners = max(1, num_accounts // 5)

    # Use seed based on num_accounts for reproducibility across runs
    seed = args.seed if args.seed is not None else 42 + num_accounts
    random.seed(seed)

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("Banking Dataset Generator")
    print("=" * 60)

    # Generate data
    print(f"\n1. Generating {num_owners} owners...")
    owners = generate_owners(num_owners)

    print(f"\n2. Generating {num_accounts} accounts...")
    accounts = generate_accounts(num_accounts, num_owners)

    print(f"\n3. Generating {num_transactions} transactions...")
    transactions = generate_transactions(accounts, num_transactions)

    # Validate data
    validate_data(accounts, transactions)
    print("\n✓ All data validated successfully!")

    # Write to CSV files
    print("\n4. Writing CSV files...")
    write_csv(output_dir, 'owner.csv', owners, ['ow_id', 'name_placeholder'])
    write_csv(output_dir, 'account.csv', accounts, ['account_id', 'balance', 'owner_id'])
    write_csv(output_dir, 'txn.csv', transactions, ['acc_from', 'acc_to', 'amount', 'txn_time'])

    print("\n" + "=" * 60)
    print("Dataset generation complete!")
    print(f"Output directory: {output_dir.absolute()}")
    print("=" * 60)


if __name__ == '__main__':
    main()
