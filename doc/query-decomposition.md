# Query Decomposition Algorithm

This document describes the query decomposition for chain/branch queries in the Oblivious Multi-Way Band Joins system.

## Overview

Query decomposition transforms complex banking chain/branch queries into simpler multi-way joins over pre-computed one-hop tables.

**Decomposition Principle:**
- Every transaction edge becomes a one-hop join
- Consecutive hops share overlapping account nodes (dest of left = src of right)
- All hops are aliases over a single pre-computed hop table
- Filters are distributed to hops that contain the filtered account

## Example

### Input Query (chain4)

```sql
SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2,
              account AS a3, txn AS t3, account AS a4
WHERE a1.account_id = t1.acc_from
  AND a2.account_id = t1.acc_to
  AND a2.account_id = t2.acc_from
  AND a3.account_id = t2.acc_to
  AND a3.account_id = t3.acc_from
  AND a4.account_id = t3.acc_to
  AND a1.owner_id = 52
  AND a4.owner_id = 45;
```

### Chain Structure

```
a1 ──t1──> a2 ──t2──> a3 ──t3──> a4
│                                │
└── owner_id=52                  └── owner_id=45
```

### Decomposition (3 hops)

```
h1: a1 ══t1══> a2
h2: a2 ══t2══> a3   (overlaps: h1.dest = h2.src at a2)
h3: a3 ══t3══> a4   (overlaps: h2.dest = h3.src at a3)
```

### Rewritten Query

```sql
SELECT * FROM hop AS h1, hop AS h2, hop AS h3
WHERE h1.account_dest_account_id = h2.account_src_account_id
  AND h2.account_dest_account_id = h3.account_src_account_id
  AND h1.account_src_owner_id = 52
  AND h3.account_dest_owner_id = 45;
```

## Branch Query Example

### Input Query (branch)

```
a1 -> t1 -> a2 -> t2 -> a3 -> t3 -> a4 -> t4 -> a5
                          \
                           -> t5 -> a6 -> t6 -> a7
```

### Decomposition (6 hops)

```
h1: a1 -> t1 -> a2
h2: a2 -> t2 -> a3
h3: a3 -> t3 -> a4   (a3 is branch point)
h4: a3 -> t5 -> a6   (a3 is branch point)
h5: a4 -> t4 -> a5
h6: a6 -> t6 -> a7
```

### Rewritten Query

```sql
SELECT * FROM hop AS h1, hop AS h2, hop AS h3, hop AS h4, hop AS h5, hop AS h6
WHERE h1.account_dest_account_id = h2.account_src_account_id
  AND h2.account_dest_account_id = h3.account_src_account_id
  AND h3.account_src_account_id = h4.account_src_account_id   -- branch at a3
  AND h3.account_dest_account_id = h5.account_src_account_id
  AND h4.account_dest_account_id = h6.account_src_account_id
  AND <filters>;
```

## Algorithm Steps

1. **Parse Query** - Extract tables, join conditions, and filter conditions
2. **Build Graph** - Construct transaction edge graph (account → txn → account)
3. **Parse Filters** - Extract filter conditions into FilterInfo objects
4. **Build Decomposition** - BFS traversal creating one hop per transaction edge
5. **Distribute Filters** - Assign each filter to first hop containing that account
6. **Generate Join Conditions** - Create joins for overlapping account nodes
7. **Generate Filter Conditions** - Map original filters to hop column names
8. **Output Query** - Assemble final SQL

## Code Reference

### Data Structures

| Structure | File | Lines | Purpose |
|-----------|------|-------|---------|
| `AccountInfo` | `scripts/rewrite_chain_query.py` | 31-34 | Account alias in the query |
| `TxnInfo` | `scripts/rewrite_chain_query.py` | 37-42 | Transaction edge (src_account → dest_account) |
| `FilterInfo` | `scripts/rewrite_chain_query.py` | 45-51 | Filter condition on an account |
| `QueryElement` | `scripts/rewrite_chain_query.py` | 54-60 | A hop in the decomposed query |

### Query Rewriting Functions

| Function | File | Lines | Purpose |
|----------|------|-------|---------|
| `_extract_number()` | `scripts/rewrite_chain_query.py` | 63-66 | Extract numeric suffix from alias |
| `parse_banking_query()` | `scripts/rewrite_chain_query.py` | 69-105 | Extract tables, joins, filters from SQL |
| `build_graph()` | `scripts/rewrite_chain_query.py` | 108-142 | Build txn→account edge graph |
| `parse_filters()` | `scripts/rewrite_chain_query.py` | 145-167 | Parse filter conditions into FilterInfo |
| `build_filter_independent_decomposition()` | `scripts/rewrite_chain_query.py` | 170-241 | Create one hop per transaction edge |
| `distribute_filters_to_hops()` | `scripts/rewrite_chain_query.py` | 244-272 | Assign filters to appropriate hops |
| `generate_join_conditions()` | `scripts/rewrite_chain_query.py` | 275-317 | Generate join conditions for overlapping nodes |
| `generate_filter_conditions()` | `scripts/rewrite_chain_query.py` | 320-335 | Map original filters to hop columns |
| `generate_optimized_query()` | `scripts/rewrite_chain_query.py` | 338-358 | Assemble final SQL |
| `rewrite_query()` | `scripts/rewrite_chain_query.py` | 361-384 | Main orchestration function |
| `main()` | `scripts/rewrite_chain_query.py` | 387-406 | CLI entry point |

### One-Hop Join (ObliGraph)

| Function | File | Lines | Purpose |
|----------|------|-------|---------|
| `writeTableToCSV()` | `obligraph/src/banking_onehop.cpp` | 23-52 | Write hop result to CSV |
| `main()` | `obligraph/src/banking_onehop.cpp` | 54-128 | Execute one-hop and output result |

### Pipeline Scripts

| Script | File | Purpose |
|--------|------|---------|
| Query Decomposition Pipeline | `scripts/run_decomposed_pipeline.sh` | End-to-end execution with timing |
| Benchmark Script | `scripts/benchmark_decomposition.sh` | Compare decomposed vs naive performance |

### Test Queries

| Query | File | Description |
|-------|------|-------------|
| Chain-4 Filtered | `input/queries/banking_chain4_filtered.sql` | 4-account chain with filters on endpoints |
| Chain-3 Filtered | `input/queries/banking_chain_filtered.sql` | 3-account chain with filters on endpoints |
| Branch Filtered | `input/queries/banking_branch_filtered.sql` | Branch query with filters on leaf nodes |

## Hop Table Schema

The one-hop join produces a denormalized table with columns from both endpoint accounts and the edge:

```
account_src_account_id, account_src_balance, account_src_owner_id,
txn_amount, txn_time,
account_dest_account_id, account_dest_balance, account_dest_owner_id
```

## Algorithm: Filter-Independent Decomposition

```python
def build_filter_independent_decomposition(txns, accounts):
    elements = []
    hop_counter = 1
    visited_txns = set()

    # Build adjacency: account -> [(txn, dest_account), ...]
    outgoing = defaultdict(list)
    for txn_alias, txn_info in txns.items():
        outgoing[txn_info.src_account].append((txn_alias, txn_info.dest_account))

    # Find starting accounts (no incoming edges)
    dest_accounts = {t.dest_account for t in txns.values()}
    src_accounts = {t.src_account for t in txns.values()}
    starting_accounts = src_accounts - dest_accounts

    # BFS from each starting account
    for start in sorted(starting_accounts):
        queue = [start]
        while queue:
            current = queue.pop(0)
            for txn_alias, dest in sorted(outgoing[current]):
                if txn_alias in visited_txns:
                    continue
                visited_txns.add(txn_alias)

                hop = QueryElement(f"h{hop_counter}", current, txn_alias, dest)
                elements.append(hop)
                hop_counter += 1
                queue.append(dest)

    return elements
```

## Filter Distribution

Filters are assigned to the **first hop** that contains the filtered account:

```python
for elem in elements:
    if elem.src_account in filters and not assigned:
        hop_filters[elem.alias].append(("src", filter))
    if elem.dest_account in filters and not assigned:
        hop_filters[elem.alias].append(("dest", filter))
```

## Usage

### Rewrite a Query

```bash
python3 scripts/rewrite_chain_query.py input/queries/banking_chain4_filtered.sql output.sql
```

### Run Full Pipeline

```bash
./scripts/run_decomposed_pipeline.sh input/queries/banking_chain4_filtered.sql input/plaintext/banking output.csv
```

### Benchmark Decomposition

```bash
./scripts/benchmark_decomposition.sh input/queries/banking_chain4_filtered.sql input/plaintext/banking 5
```

## Related Documentation

- [CLAUDE.md](../CLAUDE.md) - Project overview and build instructions
- [TDX_MIGRATION_SUMMARY.md](../TDX_MIGRATION_SUMMARY.md) - TDX architecture details
- [plan/filter-independent-decomposition.md](../plan/filter-independent-decomposition.md) - Implementation plan
