#!/usr/bin/env python3
"""
Query rewriter for chain/branch queries using optimized hop decomposition.

OPTIMIZATION PRINCIPLE:
- Use one-hop ONLY where we need account attributes (for filtering)
- Use plain txn tables for intermediate connections

Example for chain4 (a1 --t1-- a2 --t2-- a3 --t3-- a4):
  Filters: a1.owner_id=52, a4.owner_id=45

  Naive: h1, h2, h3 (3 hops)
  Optimized: h1, t2, h2 (2 hops + 1 txn)
    - h1: a1→t1→a2 (provides a1's filter)
    - t2: links a2→a3 (just the edge)
    - h2: a3→t3→a4 (provides a4's filter)
"""

import re
import sys
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Set, Optional


@dataclass
class AccountInfo:
    """Information about an account alias."""
    alias: str
    has_filter: bool = False
    filter_column: Optional[str] = None
    filter_value: Optional[str] = None


@dataclass
class TxnInfo:
    """Information about a transaction edge."""
    alias: str
    src_account: str  # acc_from
    dest_account: str  # acc_to


@dataclass
class HopInfo:
    """Information about a one-hop join."""
    hop_alias: str
    src_account: str
    txn: str
    dest_account: str


@dataclass
class QueryElement:
    """An element in the decomposed query (either hop or txn)."""
    element_type: str  # "hop" or "txn"
    alias: str
    # For hop: src_account, txn, dest_account
    # For txn: src_account (acc_from), dest_account (acc_to)
    src_account: str
    dest_account: str
    txn: Optional[str] = None  # Only for hop


def parse_banking_query(sql: str) -> Tuple[List[Tuple[str, str]], List[str], List[str]]:
    """
    Parse a banking query to extract tables, join conditions, and filters.
    """
    sql = ' '.join(sql.split())

    # Extract FROM clause tables
    from_match = re.search(r'FROM\s+(.+?)\s+WHERE', sql, re.IGNORECASE)
    if not from_match:
        raise ValueError("Could not parse FROM clause")

    from_clause = from_match.group(1)
    table_pattern = re.compile(r'(\w+)\s+AS\s+(\w+)', re.IGNORECASE)
    tables = table_pattern.findall(from_clause)

    # Extract WHERE clause
    where_match = re.search(r'WHERE\s+(.+?)(?:;|$)', sql, re.IGNORECASE)
    if not where_match:
        raise ValueError("Could not parse WHERE clause")

    where_clause = where_match.group(1)
    conditions = [c.strip() for c in re.split(r'\s+AND\s+', where_clause, flags=re.IGNORECASE)]

    join_conditions = []
    filter_conditions = []

    for cond in conditions:
        dot_count = cond.count('.')
        if dot_count >= 2:
            join_conditions.append(cond)
        else:
            filter_conditions.append(cond)

    return tables, join_conditions, filter_conditions


def build_graph(tables: List[Tuple[str, str]], join_conditions: List[str]) -> Tuple[Dict[str, TxnInfo], Dict[str, AccountInfo]]:
    """
    Build the join graph from tables and join conditions.

    Returns:
        (txn_info_map, account_info_map)
    """
    accounts: Dict[str, AccountInfo] = {}
    txns: Dict[str, TxnInfo] = {}

    # Initialize accounts
    for table_name, alias in tables:
        if table_name.lower() == 'account':
            accounts[alias] = AccountInfo(alias=alias)

    # Build txn edges
    for cond in join_conditions:
        # Match: aX.account_id = tY.acc_from
        from_match = re.match(r'(\w+)\.account_id\s*=\s*(\w+)\.acc_from', cond)
        if from_match:
            account, txn = from_match.group(1), from_match.group(2)
            if txn not in txns:
                txns[txn] = TxnInfo(alias=txn, src_account='', dest_account='')
            txns[txn].src_account = account
            continue

        # Match: aX.account_id = tY.acc_to
        to_match = re.match(r'(\w+)\.account_id\s*=\s*(\w+)\.acc_to', cond)
        if to_match:
            account, txn = to_match.group(1), to_match.group(2)
            if txn not in txns:
                txns[txn] = TxnInfo(alias=txn, src_account='', dest_account='')
            txns[txn].dest_account = account

    return txns, accounts


def identify_filtered_accounts(accounts: Dict[str, AccountInfo], filter_conditions: List[str]) -> Set[str]:
    """Identify which accounts have filter conditions."""
    filtered = set()

    for cond in filter_conditions:
        match = re.match(r'(\w+)\.(\w+)\s*=\s*(.+)', cond)
        if match:
            account, column, value = match.groups()
            if account in accounts:
                accounts[account].has_filter = True
                accounts[account].filter_column = column
                accounts[account].filter_value = value
                filtered.add(account)

    return filtered


def find_hop_for_account(account: str, txns: Dict[str, TxnInfo], position: str) -> Optional[Tuple[str, str, str]]:
    """
    Find the hop that covers an account at the specified position.

    Args:
        account: The account alias to cover
        txns: Map of txn alias to TxnInfo
        position: "src" or "dest" - where the account should be in the hop

    Returns:
        (src_account, txn_alias, dest_account) or None if not found
    """
    for txn_alias, txn_info in txns.items():
        if position == "src" and txn_info.src_account == account:
            return (txn_info.src_account, txn_alias, txn_info.dest_account)
        elif position == "dest" and txn_info.dest_account == account:
            return (txn_info.src_account, txn_alias, txn_info.dest_account)
    return None


def determine_hop_position(account: str, txns: Dict[str, TxnInfo], filtered_accounts: Set[str]) -> str:
    """
    Determine if a filtered account should be hop's src or dest.

    Heuristic:
    - If account is only a source (acc_from) in txns → "src"
    - If account is only a dest (acc_to) in txns → "dest"
    - If both, prefer the direction that doesn't overlap with another filtered account
    """
    is_src = any(t.src_account == account for t in txns.values())
    is_dest = any(t.dest_account == account for t in txns.values())

    if is_src and not is_dest:
        return "src"
    elif is_dest and not is_src:
        return "dest"
    else:
        # Check if being src would overlap with another filtered account
        for txn in txns.values():
            if txn.src_account == account:
                if txn.dest_account in filtered_accounts:
                    # The dest is also filtered, so this account should be src
                    # (the dest will be handled by another hop)
                    return "src"
        # Default to dest (end of chain)
        return "dest"


def build_optimized_decomposition(
    txns: Dict[str, TxnInfo],
    accounts: Dict[str, AccountInfo],
    filtered_accounts: Set[str]
) -> List[QueryElement]:
    """
    Build the optimized query decomposition.

    Strategy:
    1. For each filtered account, create a hop that covers it
    2. Use plain txn tables to fill gaps between hops
    """
    elements: List[QueryElement] = []
    used_txns: Set[str] = set()
    hop_counter = 1

    # Sort filtered accounts by their position in the chain
    # (based on transaction order)
    txn_order = sorted(txns.keys(), key=lambda x: int(re.search(r'\d+', x).group()) if re.search(r'\d+', x) else 0)

    # Create hops for filtered accounts, sorted by their txn position
    hops_by_account: Dict[str, QueryElement] = {}

    # Sort filtered accounts by their earliest txn position
    def account_txn_position(acc: str) -> int:
        for txn_alias in txn_order:
            txn_info = txns[txn_alias]
            if txn_info.src_account == acc or txn_info.dest_account == acc:
                return int(re.search(r'\d+', txn_alias).group()) if re.search(r'\d+', txn_alias) else 0
        return 999

    sorted_filtered = sorted(filtered_accounts, key=account_txn_position)

    for account in sorted_filtered:
        position = determine_hop_position(account, txns, filtered_accounts)
        hop_tuple = find_hop_for_account(account, txns, position)

        if hop_tuple:
            src, txn, dest = hop_tuple
            hop = QueryElement(
                element_type="hop",
                alias=f"h{hop_counter}",
                src_account=src,
                dest_account=dest,
                txn=txn
            )
            hops_by_account[account] = hop
            used_txns.add(txn)
            hop_counter += 1

    # Build the chain of elements by following txn order
    account_to_element: Dict[str, Tuple[QueryElement, str]] = {}  # account -> (element, position)

    for account, hop in hops_by_account.items():
        position = "src" if hop.src_account == account else "dest"
        account_to_element[hop.src_account] = (hop, "src")
        account_to_element[hop.dest_account] = (hop, "dest")

    # Add plain txn elements for gaps
    for txn_alias in txn_order:
        if txn_alias not in used_txns:
            txn_info = txns[txn_alias]
            txn_element = QueryElement(
                element_type="txn",
                alias=txn_alias,
                src_account=txn_info.src_account,
                dest_account=txn_info.dest_account
            )
            elements.append(txn_element)
            account_to_element[txn_info.src_account] = (txn_element, "src")
            account_to_element[txn_info.dest_account] = (txn_element, "dest")

    # Add hops to elements (sorted by txn order)
    for hop in sorted(hops_by_account.values(), key=lambda h: int(re.search(r'\d+', h.txn).group()) if re.search(r'\d+', h.txn) else 0):
        elements.append(hop)

    # Sort all elements by their txn order
    def element_order(e):
        txn = e.txn if e.element_type == "hop" else e.alias
        match = re.search(r'\d+', txn)
        return int(match.group()) if match else 0

    elements.sort(key=element_order)

    return elements


def generate_join_conditions(elements: List[QueryElement], txns: Dict[str, TxnInfo]) -> List[str]:
    """Generate join conditions between elements."""
    conditions = []

    # Build account to element mapping
    account_positions: Dict[str, List[Tuple[QueryElement, str]]] = {}

    for elem in elements:
        if elem.src_account not in account_positions:
            account_positions[elem.src_account] = []
        if elem.dest_account not in account_positions:
            account_positions[elem.dest_account] = []

        if elem.element_type == "hop":
            account_positions[elem.src_account].append((elem, "src"))
            account_positions[elem.dest_account].append((elem, "dest"))
        else:  # txn
            account_positions[elem.src_account].append((elem, "from"))
            account_positions[elem.dest_account].append((elem, "to"))

    # Generate join conditions for accounts that appear in multiple elements
    generated = set()

    for account, positions in account_positions.items():
        if len(positions) > 1:
            for i in range(len(positions) - 1):
                elem1, pos1 = positions[i]
                elem2, pos2 = positions[i + 1]

                # Generate column references
                if elem1.element_type == "hop":
                    col1 = f"{elem1.alias}.account_{pos1}_id"
                else:
                    col1 = f"{elem1.alias}.acc_{pos1}"

                if elem2.element_type == "hop":
                    col2 = f"{elem2.alias}.account_{pos2}_id"
                else:
                    col2 = f"{elem2.alias}.acc_{pos2}"

                join_key = tuple(sorted([col1, col2]))
                if join_key not in generated:
                    conditions.append(f"{col1} = {col2}")
                    generated.add(join_key)

    return conditions


def generate_filter_conditions(elements: List[QueryElement], accounts: Dict[str, AccountInfo]) -> List[str]:
    """Generate filter conditions mapped to the new schema."""
    conditions = []

    for elem in elements:
        if elem.element_type == "hop":
            # Check if src or dest account has filter
            src_account = accounts.get(elem.src_account)
            dest_account = accounts.get(elem.dest_account)

            if src_account and src_account.has_filter:
                col = f"{elem.alias}.account_src_{src_account.filter_column}"
                conditions.append(f"{col} = {src_account.filter_value}")

            if dest_account and dest_account.has_filter:
                col = f"{elem.alias}.account_dest_{dest_account.filter_column}"
                conditions.append(f"{col} = {dest_account.filter_value}")

    return conditions


def generate_optimized_query(elements: List[QueryElement], txns: Dict[str, TxnInfo], accounts: Dict[str, AccountInfo]) -> str:
    """Generate the optimized SQL query."""

    # FROM clause
    from_parts = []
    for elem in elements:
        if elem.element_type == "hop":
            from_parts.append(f"hop AS {elem.alias}")
        else:
            from_parts.append(f"txn AS {elem.alias}")

    from_clause = "SELECT * FROM " + ", ".join(from_parts)

    # WHERE clause
    join_conditions = generate_join_conditions(elements, txns)
    filter_conditions = generate_filter_conditions(elements, accounts)

    all_conditions = join_conditions + filter_conditions
    where_clause = "WHERE " + "\n  AND ".join(all_conditions)

    return f"{from_clause}\n{where_clause};"


def rewrite_query(input_sql: str) -> str:
    """Main function to rewrite a banking query with optimization."""
    tables, join_conditions, filter_conditions = parse_banking_query(input_sql)
    txns, accounts = build_graph(tables, join_conditions)
    filtered_accounts = identify_filtered_accounts(accounts, filter_conditions)

    print(f"Accounts with filters: {filtered_accounts}", file=sys.stderr)

    elements = build_optimized_decomposition(txns, accounts, filtered_accounts)

    print(f"Decomposition ({len([e for e in elements if e.element_type == 'hop'])} hops, "
          f"{len([e for e in elements if e.element_type == 'txn'])} txns):", file=sys.stderr)
    for elem in elements:
        if elem.element_type == "hop":
            print(f"  {elem.alias}: {elem.src_account} -> {elem.txn} -> {elem.dest_account} (hop)", file=sys.stderr)
        else:
            print(f"  {elem.alias}: {elem.src_account} -> {elem.dest_account} (txn)", file=sys.stderr)

    return generate_optimized_query(elements, txns, accounts)


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
        print(f"Wrote optimized query to {output_path}", file=sys.stderr)
    else:
        print(output_sql)


if __name__ == "__main__":
    main()
