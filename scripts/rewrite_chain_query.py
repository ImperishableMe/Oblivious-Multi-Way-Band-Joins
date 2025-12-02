#!/usr/bin/env python3
"""
Query rewriter for chain/branch queries using hop decomposition.

This script transforms banking chain/branch queries to use pre-computed
hop results instead of individual account and txn tables.

Original chain pattern:
  a1 --t1-- a2 --t2-- a3 --t3-- a4

Becomes:
  h1 ------ h2 ------ h3

Where each hop (h) represents an (account -> txn -> account) join.
"""

import re
import sys
from dataclasses import dataclass
from typing import List, Dict, Tuple, Optional


@dataclass
class HopInfo:
    """Information about a single hop in the decomposed query."""
    hop_alias: str      # e.g., "h1"
    src_account: str    # e.g., "a1"
    txn: str            # e.g., "t1"
    dest_account: str   # e.g., "a2"


def parse_banking_query(sql: str) -> Tuple[List[str], List[str], List[str]]:
    """
    Parse a banking query to extract tables, join conditions, and filters.

    Returns:
        (table_aliases, join_conditions, filter_conditions)
    """
    # Remove newlines and extra spaces
    sql = ' '.join(sql.split())

    # Extract FROM clause tables
    from_match = re.search(r'FROM\s+(.+?)\s+WHERE', sql, re.IGNORECASE)
    if not from_match:
        raise ValueError("Could not parse FROM clause")

    from_clause = from_match.group(1)
    # Parse "table AS alias" patterns
    table_pattern = re.compile(r'(\w+)\s+AS\s+(\w+)', re.IGNORECASE)
    tables = table_pattern.findall(from_clause)

    # Extract WHERE clause
    where_match = re.search(r'WHERE\s+(.+?)(?:;|$)', sql, re.IGNORECASE)
    if not where_match:
        raise ValueError("Could not parse WHERE clause")

    where_clause = where_match.group(1)

    # Split into individual conditions
    conditions = [c.strip() for c in re.split(r'\s+AND\s+', where_clause, flags=re.IGNORECASE)]

    # Separate join conditions from filter conditions
    join_conditions = []
    filter_conditions = []

    for cond in conditions:
        # Join condition: contains two qualified names (e.g., a1.x = t1.y)
        dot_count = cond.count('.')
        if dot_count >= 2:
            join_conditions.append(cond)
        else:
            filter_conditions.append(cond)

    return tables, join_conditions, filter_conditions


def identify_hops(tables: List[Tuple[str, str]], join_conditions: List[str]) -> List[HopInfo]:
    """
    Identify (account -> txn -> account) hops from the join structure.

    Each hop is identified by finding patterns:
    - aX.account_id = tY.acc_from (source account to txn)
    - aZ.account_id = tY.acc_to (txn to dest account)
    """
    # Build a map of txn -> (src_account, dest_account)
    txn_to_accounts: Dict[str, Dict[str, str]] = {}

    for cond in join_conditions:
        # Match: aX.account_id = tY.acc_from or tY.acc_from = aX.account_id
        from_match = re.match(r'(\w+)\.account_id\s*=\s*(\w+)\.acc_from', cond)
        if not from_match:
            from_match = re.match(r'(\w+)\.acc_from\s*=\s*(\w+)\.account_id', cond)
            if from_match:
                from_match = (from_match.group(2), from_match.group(1))
            else:
                from_match = None

        if from_match:
            if isinstance(from_match, tuple):
                account, txn = from_match
            else:
                account, txn = from_match.group(1), from_match.group(2)
            if txn not in txn_to_accounts:
                txn_to_accounts[txn] = {}
            txn_to_accounts[txn]['src'] = account
            continue

        # Match: aX.account_id = tY.acc_to or tY.acc_to = aX.account_id
        to_match = re.match(r'(\w+)\.account_id\s*=\s*(\w+)\.acc_to', cond)
        if not to_match:
            to_match = re.match(r'(\w+)\.acc_to\s*=\s*(\w+)\.account_id', cond)
            if to_match:
                to_match = (to_match.group(2), to_match.group(1))
            else:
                to_match = None

        if to_match:
            if isinstance(to_match, tuple):
                account, txn = to_match
            else:
                account, txn = to_match.group(1), to_match.group(2)
            if txn not in txn_to_accounts:
                txn_to_accounts[txn] = {}
            txn_to_accounts[txn]['dest'] = account

    # Create hop list sorted by txn order (t1, t2, t3, ...)
    hops = []
    txn_aliases = sorted(txn_to_accounts.keys(), key=lambda x: int(re.search(r'\d+', x).group()))

    for i, txn in enumerate(txn_aliases, 1):
        accounts = txn_to_accounts[txn]
        if 'src' in accounts and 'dest' in accounts:
            hops.append(HopInfo(
                hop_alias=f"h{i}",
                src_account=accounts['src'],
                txn=txn,
                dest_account=accounts['dest']
            ))

    return hops


def find_account_hop_position(account: str, hops: List[HopInfo]) -> Tuple[str, str]:
    """
    Find which hop and position (src/dest) an account alias maps to.

    Returns: (hop_alias, "src" or "dest")
    """
    for hop in hops:
        if hop.src_account == account:
            return (hop.hop_alias, "src")
        if hop.dest_account == account:
            return (hop.hop_alias, "dest")
    raise ValueError(f"Account {account} not found in any hop")


def generate_decomposed_query(hops: List[HopInfo], filter_conditions: List[str]) -> str:
    """Generate the decomposed query using hop tables."""

    # FROM clause
    from_parts = [f"hop AS {hop.hop_alias}" for hop in hops]
    from_clause = "SELECT * FROM " + ", ".join(from_parts)

    # WHERE clause - join conditions between hops
    where_parts = []

    # Track which accounts are shared between hops
    account_to_hops: Dict[str, List[Tuple[str, str]]] = {}
    for hop in hops:
        if hop.src_account not in account_to_hops:
            account_to_hops[hop.src_account] = []
        account_to_hops[hop.src_account].append((hop.hop_alias, "src"))

        if hop.dest_account not in account_to_hops:
            account_to_hops[hop.dest_account] = []
        account_to_hops[hop.dest_account].append((hop.hop_alias, "dest"))

    # Generate join conditions for shared accounts
    generated_joins = set()
    for account, positions in account_to_hops.items():
        if len(positions) > 1:
            # This account is shared between multiple hops
            for i in range(len(positions) - 1):
                hop1, pos1 = positions[i]
                hop2, pos2 = positions[i + 1]

                # Convert position to column name
                col1 = f"{hop1}.account_{pos1}_id"
                col2 = f"{hop2}.account_{pos2}_id"

                join_key = tuple(sorted([col1, col2]))
                if join_key not in generated_joins:
                    where_parts.append(f"{col1} = {col2}")
                    generated_joins.add(join_key)

    # Transform filter conditions
    for cond in filter_conditions:
        # Parse: aX.column_name = value
        match = re.match(r'(\w+)\.(\w+)\s*=\s*(.+)', cond)
        if match:
            account, column, value = match.groups()
            try:
                hop_alias, position = find_account_hop_position(account, hops)
                new_column = f"account_{position}_{column}"
                where_parts.append(f"{hop_alias}.{new_column} = {value}")
            except ValueError:
                # Account not found - keep original condition
                where_parts.append(cond)
        else:
            where_parts.append(cond)

    where_clause = "WHERE " + "\n  AND ".join(where_parts)

    return f"{from_clause}\n{where_clause};"


def rewrite_query(input_sql: str) -> str:
    """Main function to rewrite a banking query."""
    tables, join_conditions, filter_conditions = parse_banking_query(input_sql)
    hops = identify_hops(tables, join_conditions)

    print(f"Identified {len(hops)} hops:", file=sys.stderr)
    for hop in hops:
        print(f"  {hop.hop_alias}: {hop.src_account} -> {hop.txn} -> {hop.dest_account}", file=sys.stderr)

    return generate_decomposed_query(hops, filter_conditions)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.sql> [output.sql]")
        print("  If output.sql is not specified, prints to stdout")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    with open(input_path, 'r') as f:
        input_sql = f.read()

    output_sql = rewrite_query(input_sql)

    if output_path:
        with open(output_path, 'w') as f:
            f.write(output_sql)
        print(f"Wrote decomposed query to {output_path}", file=sys.stderr)
    else:
        print(output_sql)


if __name__ == "__main__":
    main()
