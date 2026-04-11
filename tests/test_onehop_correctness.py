#!/usr/bin/env python3
"""
test_onehop_correctness.py

Correctness test for the oneHop join by comparing its output against a SQLite baseline.

The test runs the oneHop binary on a banking dataset, then runs an equivalent SQL query
in SQLite on the same raw CSV data, and asserts that the two result sets are identical
(after sorting both by txn_id).

The oneHop output schema for the banking self-referential join is:
    txn_id, acc_from, acc_to, amount, txn_time,
    account_src_account_id, account_src_balance, account_src_owner_id,
    account_dest_account_id, account_dest_balance, account_dest_owner_id

The SQLite query is constructed dynamically to produce the same schema.

Usage:
    python3 tests/test_onehop_correctness.py <data_dir> <banking_onehop_binary>

Example:
    python3 tests/test_onehop_correctness.py input/plaintext/banking_1k obligraph/build/banking_onehop

The test works for any banking dataset directory that contains account.csv and txn.csv
with the expected schemas, regardless of dataset size.
"""

import csv
import os
import sqlite3
import subprocess
import sys
import tempfile


# ---------------------------------------------------------------------------
# Expected output schema (must match the column order that oneHop produces)
# ---------------------------------------------------------------------------
# Column order matches the oneHop output exactly.
# Edge columns are projected through a set<string> internally, so they appear
# in alphabetical order: acc_from, acc_to, amount, txn_id, txn_time.
EXPECTED_COLUMNS = [
    "acc_from",
    "acc_to",
    "amount",
    "txn_id",
    "txn_time",
    "account_src_account_id",
    "account_src_balance",
    "account_src_owner_id",
    "account_dest_account_id",
    "account_dest_balance",
    "account_dest_owner_id",
]

SORT_KEY = "txn_id"


# ---------------------------------------------------------------------------
# Run the oneHop binary
# ---------------------------------------------------------------------------
def run_onehop(binary: str, data_dir: str, output_path: str) -> None:
    result = subprocess.run(
        [binary, data_dir, output_path],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"banking_onehop exited with code {result.returncode}.\n"
            f"stderr:\n{result.stderr}"
        )


# ---------------------------------------------------------------------------
# Load a CSV into a list of row-dicts with all values as int
# ---------------------------------------------------------------------------
def load_csv_as_ints(path: str) -> list:
    rows = []
    with open(path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append({k: int(v) for k, v in row.items()})
    return rows


# ---------------------------------------------------------------------------
# Run the SQLite baseline
# ---------------------------------------------------------------------------
def run_sqlite_baseline(data_dir: str) -> list:
    account_path = os.path.join(data_dir, "account.csv")
    txn_path = os.path.join(data_dir, "txn.csv")

    conn = sqlite3.connect(":memory:")
    cur = conn.cursor()

    # Create and populate account table
    cur.execute(
        "CREATE TABLE account (account_id INTEGER, balance INTEGER, owner_id INTEGER)"
    )
    with open(account_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            cur.execute(
                "INSERT INTO account VALUES (?, ?, ?)",
                (int(row["account_id"]), int(row["balance"]), int(row["owner_id"])),
            )

    # Create and populate txn table
    cur.execute(
        "CREATE TABLE txn "
        "(txn_id INTEGER, acc_from INTEGER, acc_to INTEGER, amount INTEGER, txn_time INTEGER)"
    )
    with open(txn_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            cur.execute(
                "INSERT INTO txn VALUES (?, ?, ?, ?, ?)",
                (
                    int(row["txn_id"]),
                    int(row["acc_from"]),
                    int(row["acc_to"]),
                    int(row["amount"]),
                    int(row["txn_time"]),
                ),
            )

    # Equivalent one-hop query: (account) -[txn]-> (account)
    # Column order matches EXPECTED_COLUMNS exactly.
    # Edge columns come first in alphabetical order (acc_from, acc_to, amount, txn_id, txn_time)
    # because oneHop projects them through a set<string> internally.
    cur.execute(
        """
        SELECT
            t.acc_from,
            t.acc_to,
            t.amount,
            t.txn_id,
            t.txn_time,
            a_src.account_id  AS account_src_account_id,
            a_src.balance     AS account_src_balance,
            a_src.owner_id    AS account_src_owner_id,
            a_dst.account_id  AS account_dest_account_id,
            a_dst.balance     AS account_dest_balance,
            a_dst.owner_id    AS account_dest_owner_id
        FROM txn t
        JOIN account a_src ON a_src.account_id = t.acc_from
        JOIN account a_dst ON a_dst.account_id = t.acc_to
        """
    )

    columns = [desc[0] for desc in cur.description]
    rows = []
    for row in cur.fetchall():
        rows.append(dict(zip(columns, [int(v) for v in row])))

    conn.close()
    return rows


# ---------------------------------------------------------------------------
# Verify that oneHop output has the expected column schema
# ---------------------------------------------------------------------------
def check_schema(onehop_rows: list, path: str) -> None:
    if not onehop_rows:
        return  # empty result — schema check not possible, skip
    actual_cols = list(onehop_rows[0].keys())
    if actual_cols != EXPECTED_COLUMNS:
        raise AssertionError(
            f"oneHop output schema mismatch.\n"
            f"  Expected: {EXPECTED_COLUMNS}\n"
            f"  Got:      {actual_cols}\n"
            f"  (from {path})"
        )


# ---------------------------------------------------------------------------
# Sort and compare
# ---------------------------------------------------------------------------
def compare(onehop_rows: list, sqlite_rows: list) -> bool:
    onehop_sorted = sorted(onehop_rows, key=lambda r: r[SORT_KEY])
    sqlite_sorted = sorted(sqlite_rows, key=lambda r: r[SORT_KEY])

    if len(onehop_sorted) != len(sqlite_sorted):
        print(
            f"FAIL: row count mismatch — oneHop={len(onehop_sorted)}, SQLite={len(sqlite_sorted)}"
        )
        return False

    mismatches = 0
    for i, (oh, sq) in enumerate(zip(onehop_sorted, sqlite_sorted)):
        if oh != sq:
            mismatches += 1
            if mismatches <= 5:  # print first 5 mismatches
                print(f"FAIL: row {i} (txn_id={oh[SORT_KEY]}) mismatch:")
                for col in EXPECTED_COLUMNS:
                    if oh.get(col) != sq.get(col):
                        print(f"  {col}: oneHop={oh.get(col)}  SQLite={sq.get(col)}")

    if mismatches > 0:
        print(f"FAIL: {mismatches} row(s) differ out of {len(onehop_sorted)} total")
        return False

    print(f"PASS: {len(onehop_sorted)} rows match exactly")
    return True


# ---------------------------------------------------------------------------
# Dataset sanity check
# ---------------------------------------------------------------------------
def check_dataset(data_dir: str) -> None:
    """
    Validate that the dataset is compatible with obligraph's key representation.

    obligraph uses bit 63 of a uint64_t key as the dummy marker (DUMMY_KEY_MSB).
    Any negative integer ID sign-extends to a uint64_t with bit 63 set, which
    obligraph silently treats as a dummy row and drops from output.

    The main SGX join pipeline uses sentinel rows with all values = -10000 as
    the last row of each table.  Those sentinel rows must NOT be present in
    datasets fed to obligraph — they will be silently dropped, causing
    oneHop output to diverge from a plain SQL baseline.
    """
    account_path = os.path.join(data_dir, "account.csv")
    txn_path = os.path.join(data_dir, "txn.csv")

    errors = []

    with open(account_path) as f:
        for i, row in enumerate(csv.DictReader(f), start=1):
            if int(row["account_id"]) <= 0:
                errors.append(
                    f"account.csv row {i}: account_id={row['account_id']} is non-positive. "
                    f"obligraph requires all IDs to be strictly positive integers. "
                    f"Sentinel rows (account_id=-10000) from the SGX join pipeline must be "
                    f"removed before passing a dataset to obligraph."
                )

    with open(txn_path) as f:
        for i, row in enumerate(csv.DictReader(f), start=1):
            for col in ("acc_from", "acc_to"):
                if int(row[col]) <= 0:
                    errors.append(
                        f"txn.csv row {i}: {col}={row[col]} is non-positive. "
                        f"Sentinel rows (all values=-10000) from the SGX join pipeline must be "
                        f"removed before passing a dataset to obligraph."
                    )
                    break  # one error per row is enough

    if errors:
        print("ERROR: Dataset contains non-positive IDs incompatible with obligraph.")
        print("       obligraph uses bit 63 of uint64_t as a dummy marker; negative integers")
        print("       sign-extend to set that bit and are silently treated as dummy rows.")
        print()
        for e in errors[:5]:
            print(f"  {e}")
        if len(errors) > 5:
            print(f"  ... and {len(errors) - 5} more.")
        sys.exit(1)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <data_dir> <banking_onehop_binary>")
        print(f"Example: {sys.argv[0]} input/plaintext/banking_1k obligraph/build/banking_onehop")
        sys.exit(1)

    data_dir = sys.argv[1]
    binary = sys.argv[2]

    if not os.path.isdir(data_dir):
        print(f"Error: data_dir '{data_dir}' does not exist")
        sys.exit(1)
    if not os.path.isfile(binary):
        print(f"Error: binary '{binary}' does not exist")
        sys.exit(1)

    print("[0/4] Checking dataset compatibility...")
    check_dataset(data_dir)

    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
        onehop_output = f.name

    try:
        print(f"[1/4] Running oneHop on {data_dir}...")
        run_onehop(binary, data_dir, onehop_output)

        print("[2/4] Running SQLite baseline...")
        onehop_rows = load_csv_as_ints(onehop_output)
        sqlite_rows = run_sqlite_baseline(data_dir)

        print(f"      oneHop rows: {len(onehop_rows)},  SQLite rows: {len(sqlite_rows)}")

        check_schema(onehop_rows, onehop_output)

        print("[3/4] Comparing results...")
        success = compare(onehop_rows, sqlite_rows)

        sys.exit(0 if success else 1)

    finally:
        os.unlink(onehop_output)


if __name__ == "__main__":
    main()
