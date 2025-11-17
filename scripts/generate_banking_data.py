#!/usr/bin/env python3
"""
Banking Dataset Generator for Oblivious Multi-Way Join Testing

Generates realistic banking data with:
- Account table: ~10,000 accounts with balances and owner IDs
- Transaction table: ~50,000 transactions between accounts
- Owner table: ~2,000 owners (for easier querying)

Uses Zipfian distribution for transaction counts to create realistic variance
(few very active accounts, most moderately active).
"""

import csv
import random
from collections import defaultdict
from pathlib import Path

# System bounds from enclave_types.h
JOIN_ATTR_MIN = -1_073_741_820
JOIN_ATTR_MAX = 1_073_741_820

# Dataset parameters
NUM_OWNERS = 2_000
NUM_ACCOUNTS = 10_000
NUM_TRANSACTIONS = 50_000

# Data ranges
MIN_BALANCE = 0
MAX_BALANCE = 1_000_000
MIN_AMOUNT = 1
MAX_AMOUNT = 100_000

# Timestamp range (days since epoch to fit system bounds)
# Using 1-365000 to represent ~1000 years of days
MIN_TIMESTAMP = 1
MAX_TIMESTAMP = 365000  # Well within JOIN_ATTR_MAX

# Output directory
OUTPUT_DIR = Path("input/plaintext/banking")

# Sentinel value for last row
SENTINEL = -10000


def zipfian_choice(items, alpha=1.5):
    """
    Select an item using Zipfian distribution (without numpy).

    Args:
        items: List of items to choose from
        alpha: Zipf parameter (higher = more skewed)

    Returns:
        Selected item
    """
    n = len(items)
    # Calculate unnormalized weights: w_i = 1/i^alpha
    weights = [1.0 / pow(i + 1, alpha) for i in range(n)]
    total_weight = sum(weights)

    # Random weighted selection
    rand_val = random.uniform(0, total_weight)
    cumulative = 0.0
    for i, weight in enumerate(weights):
        cumulative += weight
        if rand_val <= cumulative:
            return items[i]

    return items[-1]  # Fallback to last item


def generate_owners():
    """Generate owner table."""
    owners = []
    for owner_id in range(1, NUM_OWNERS + 1):
        owners.append({
            'ow_id': owner_id,  # Renamed to avoid conflict with account.owner_id
            'name_placeholder': owner_id  # Just use ID as placeholder
        })
    return owners


def generate_accounts():
    """Generate account table with realistic distribution."""
    accounts = []

    # Distribute accounts across owners (not uniform - some owners have multiple accounts)
    accounts_per_owner = defaultdict(int)

    for account_id in range(1, NUM_ACCOUNTS + 1):
        # Weighted towards some owners having multiple accounts
        # 70% have 1 account, 20% have 2-3, 10% have 4-10
        rand_val = random.random()
        if rand_val < 0.7:
            # Single account owners - evenly distributed
            owner_id = random.randint(1, NUM_OWNERS)
        else:
            # Multi-account owners - concentrated in lower IDs
            owner_id = random.randint(1, NUM_OWNERS // 5)

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


def generate_transactions(accounts):
    """Generate transaction table with Zipfian distribution."""
    transactions = []

    # Create account ID list
    account_ids = [acc['account_id'] for acc in accounts]

    # Track transaction counts per account
    txn_counts = defaultdict(int)

    for txn_id in range(1, NUM_TRANSACTIONS + 1):
        # Select source account using Zipfian distribution
        acc_from = zipfian_choice(account_ids, alpha=1.5)

        # Select destination account (uniform random, excluding source)
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

    # Print statistics
    txn_list = sorted(txn_counts.values(), reverse=True)
    print(f"\nGenerated {len(transactions)} transactions")
    print(f"Accounts with outgoing transactions: {len(txn_counts)}")
    print(f"Top 10 most active accounts: {txn_list[:10]}")
    print(f"Median transactions per account: {txn_list[len(txn_list)//2] if txn_list else 0}")
    print(f"Avg transactions per active account: {sum(txn_list) / len(txn_list):.2f}")

    # Calculate variance manually
    mean = sum(txn_list) / len(txn_list) if txn_list else 0
    variance = sum((x - mean) ** 2 for x in txn_list) / len(txn_list) if txn_list else 0
    print(f"Variance in transaction counts: {variance:.2f}")

    return transactions


def write_csv(filename, data, fieldnames):
    """Write data to CSV file with sentinel row."""
    filepath = OUTPUT_DIR / filename

    with open(filepath, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(data)

        # Add sentinel row
        sentinel_row = {field: SENTINEL for field in fieldnames}
        writer.writerow(sentinel_row)

    print(f"Wrote {len(data)} rows to {filepath}")


def validate_data(accounts, transactions):
    """Validate foreign key constraints and data bounds."""
    print("\nValidating data...")

    # Check account IDs are in valid range
    account_ids = {acc['account_id'] for acc in accounts}
    assert all(JOIN_ATTR_MIN <= aid <= JOIN_ATTR_MAX for aid in account_ids), \
        "Account IDs out of range!"

    # Check all transaction foreign keys are valid
    invalid_txns = []
    for txn in transactions:
        if txn['acc_from'] not in account_ids:
            invalid_txns.append(f"Invalid acc_from: {txn['acc_from']}")
        if txn['acc_to'] not in account_ids:
            invalid_txns.append(f"Invalid acc_to: {txn['acc_to']}")

    if invalid_txns:
        print(f"ERROR: Found {len(invalid_txns)} invalid transactions!")
        for err in invalid_txns[:10]:
            print(f"  {err}")
        raise ValueError("Foreign key validation failed!")

    # Check all values in valid range
    for acc in accounts:
        assert JOIN_ATTR_MIN <= acc['balance'] <= JOIN_ATTR_MAX, \
            f"Balance out of range: {acc['balance']}"
        assert JOIN_ATTR_MIN <= acc['owner_id'] <= JOIN_ATTR_MAX, \
            f"Owner ID out of range: {acc['owner_id']}"

    for txn in transactions:
        assert JOIN_ATTR_MIN <= txn['amount'] <= JOIN_ATTR_MAX, \
            f"Amount out of range: {txn['amount']}"
        assert JOIN_ATTR_MIN <= txn['txn_time'] <= JOIN_ATTR_MAX, \
            f"Timestamp out of range: {txn['txn_time']}"

    print("✓ All data validated successfully!")


def main():
    # Set random seed for reproducibility
    random.seed(42)

    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("Banking Dataset Generator")
    print("=" * 60)

    # Generate data
    print("\n1. Generating owners...")
    owners = generate_owners()

    print("\n2. Generating accounts...")
    accounts = generate_accounts()

    print("\n3. Generating transactions...")
    transactions = generate_transactions(accounts)

    # Validate data
    validate_data(accounts, transactions)

    # Write to CSV files
    print("\n4. Writing CSV files...")
    write_csv('owner.csv', owners, ['ow_id', 'name_placeholder'])
    write_csv('account.csv', accounts, ['account_id', 'balance', 'owner_id'])
    write_csv('txn.csv', transactions, ['acc_from', 'acc_to', 'amount', 'txn_time'])

    print("\n" + "=" * 60)
    print("Dataset generation complete!")
    print(f"Output directory: {OUTPUT_DIR.absolute()}")
    print("=" * 60)


if __name__ == '__main__':
    main()
