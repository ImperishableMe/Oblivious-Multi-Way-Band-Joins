#!/usr/bin/env python3
"""
test_pipeline_correctness.py

End-to-end correctness test for the query decomposition pipeline:
    banking_onehop → rewrite_chain_query → sgx_app

Tests that the pipeline output matches a direct SQLite baseline on the same
raw banking data for two queries:

  1. No-filter 2-hop chain (banking_chain_no_filter.sql equivalent)
  2. Filtered 2-hop chain — filter pair (a1.owner_id, a3.owner_id) is chosen
     dynamically as the pair with the most chain results in the dataset,
     guaranteeing a non-trivial test regardless of dataset size or seed.

The SQLite baseline projects columns to match the sgx_app output schema exactly,
so rows can be compared directly after sorting by (h1.txn_id, h2.txn_id).

Usage:
    python3 tests/test_pipeline_correctness.py <data_dir> <banking_onehop_binary> <sgx_app_binary>

Example:
    python3 tests/test_pipeline_correctness.py input/plaintext/banking_1k \\
        obligraph/build/banking_onehop ./sgx_app
"""

import csv
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile

# Import rewrite_chain_query from the scripts directory
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
from rewrite_chain_query import rewrite_query  # noqa: E402


# ---------------------------------------------------------------------------
# Expected output schema
# ---------------------------------------------------------------------------
# sgx_app prefixes each hop's columns with the alias (h1., h2.).
# Column order within each hop matches hop.csv column order, which follows
# the original CSV schema order (txn_id, acc_from, acc_to, amount, txn_time,
# account_src_*, account_dest_*) because obligraph skips projection entirely
# when all columns are selected, preserving the original schema order.
HOP_COLUMNS = [
    "txn_id",
    "acc_from",
    "acc_to",
    "amount",
    "txn_time",
    "account_src_account_id",
    "account_src_balance",
    "account_src_owner_id",
    "account_dest_account_id",
    "account_dest_balance",
    "account_dest_owner_id",
]

EXPECTED_COLUMNS = [f"h1.{c}" for c in HOP_COLUMNS] + [f"h2.{c}" for c in HOP_COLUMNS]

# Sort key: (h1 txn_id, h2 txn_id) uniquely identifies each chain result
# because txn_id is unique per transaction.
SORT_KEY = ("h1.txn_id", "h2.txn_id")


# ---------------------------------------------------------------------------
# Dataset sanity check
# ---------------------------------------------------------------------------
def check_dataset(data_dir: str) -> None:
    """
    Validate that the dataset is compatible with obligraph's key representation.

    obligraph uses bit 63 of a uint64_t key as the dummy marker (DUMMY_KEY_MSB).
    Any non-positive integer ID sign-extends to set that bit and is silently
    dropped from output, causing oneHop to diverge from SQLite.
    """
    account_path = os.path.join(data_dir, "account.csv")
    txn_path = os.path.join(data_dir, "txn.csv")
    errors = []

    with open(account_path) as f:
        for i, row in enumerate(csv.DictReader(f), start=1):
            if int(row["account_id"]) <= 0:
                errors.append(
                    f"account.csv row {i}: account_id={row['account_id']} is non-positive"
                )

    with open(txn_path) as f:
        for i, row in enumerate(csv.DictReader(f), start=1):
            for col in ("acc_from", "acc_to"):
                if int(row[col]) <= 0:
                    errors.append(
                        f"txn.csv row {i}: {col}={row[col]} is non-positive"
                    )
                    break

    if errors:
        print("ERROR: Dataset contains non-positive IDs incompatible with obligraph.")
        for e in errors[:5]:
            print(f"  {e}")
        if len(errors) > 5:
            print(f"  ... and {len(errors) - 5} more.")
        sys.exit(1)


# ---------------------------------------------------------------------------
# Run banking_onehop
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
# Build in-memory SQLite database from raw banking data
# ---------------------------------------------------------------------------
def build_sqlite_db(data_dir: str) -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    cur = conn.cursor()

    cur.execute(
        "CREATE TABLE account (account_id INTEGER, balance INTEGER, owner_id INTEGER)"
    )
    with open(os.path.join(data_dir, "account.csv")) as f:
        for row in csv.DictReader(f):
            cur.execute(
                "INSERT INTO account VALUES (?,?,?)",
                (int(row["account_id"]), int(row["balance"]), int(row["owner_id"])),
            )

    cur.execute(
        "CREATE TABLE txn "
        "(txn_id INTEGER, acc_from INTEGER, acc_to INTEGER, amount INTEGER, txn_time INTEGER)"
    )
    with open(os.path.join(data_dir, "txn.csv")) as f:
        for row in csv.DictReader(f):
            cur.execute(
                "INSERT INTO txn VALUES (?,?,?,?,?)",
                (
                    int(row["txn_id"]),
                    int(row["acc_from"]),
                    int(row["acc_to"]),
                    int(row["amount"]),
                    int(row["txn_time"]),
                ),
            )

    conn.commit()
    return conn


# ---------------------------------------------------------------------------
# SQLite baseline: 2-hop chain query
# ---------------------------------------------------------------------------
# Column projection maps sgx_app output columns back to original table columns.
# Decomposition: h1 = a1 -> t1 -> a2,  h2 = a2 -> t2 -> a3
#
# Note: a2 appears in both h1.account_dest_* and h2.account_src_* with identical
# values (enforced by the join condition h1.account_dest_account_id =
# h2.account_src_account_id), so duplicating a2's columns for both hops is correct.
_CHAIN2_SQL = """
SELECT
  t1.acc_from               AS "h1.acc_from",
  t1.acc_to                 AS "h1.acc_to",
  t1.amount                 AS "h1.amount",
  t1.txn_id                 AS "h1.txn_id",
  t1.txn_time               AS "h1.txn_time",
  a1.account_id             AS "h1.account_src_account_id",
  a1.balance                AS "h1.account_src_balance",
  a1.owner_id               AS "h1.account_src_owner_id",
  a2.account_id             AS "h1.account_dest_account_id",
  a2.balance                AS "h1.account_dest_balance",
  a2.owner_id               AS "h1.account_dest_owner_id",
  t2.acc_from               AS "h2.acc_from",
  t2.acc_to                 AS "h2.acc_to",
  t2.amount                 AS "h2.amount",
  t2.txn_id                 AS "h2.txn_id",
  t2.txn_time               AS "h2.txn_time",
  a2.account_id             AS "h2.account_src_account_id",
  a2.balance                AS "h2.account_src_balance",
  a2.owner_id               AS "h2.account_src_owner_id",
  a3.account_id             AS "h2.account_dest_account_id",
  a3.balance                AS "h2.account_dest_balance",
  a3.owner_id               AS "h2.account_dest_owner_id"
FROM txn t1, txn t2, account a1, account a2, account a3
WHERE a1.account_id = t1.acc_from
  AND a2.account_id = t1.acc_to
  AND a2.account_id = t2.acc_from
  AND a3.account_id = t2.acc_to
  {filter_clause}
"""


def run_sqlite_chain(
    conn: sqlite3.Connection, src_owner: int = None, dest_owner: int = None
) -> list:
    filter_parts = []
    if src_owner is not None:
        filter_parts.append(f"AND a1.owner_id = {src_owner}")
    if dest_owner is not None:
        filter_parts.append(f"AND a3.owner_id = {dest_owner}")

    cur = conn.cursor()
    cur.execute(_CHAIN2_SQL.format(filter_clause="\n  ".join(filter_parts)))

    columns = [desc[0] for desc in cur.description]
    rows = []
    for row in cur.fetchall():
        rows.append(dict(zip(columns, [int(v) for v in row])))
    return rows


# ---------------------------------------------------------------------------
# Find best (src_owner_id, dest_owner_id) filter pair
# ---------------------------------------------------------------------------
def find_best_filter_pair(conn: sqlite3.Connection) -> tuple:
    """
    Returns the (src_owner_id, dest_owner_id) pair that produces the most
    2-hop chain results. Ties broken by smallest owner_id values (deterministic).

    This ensures the filtered test exercises real matches regardless of
    dataset size or random seed.
    """
    cur = conn.cursor()
    cur.execute(
        """
        SELECT a1.owner_id, a3.owner_id, count(*) AS cnt
        FROM txn t1, txn t2, account a1, account a2, account a3
        WHERE a1.account_id = t1.acc_from
          AND a2.account_id = t1.acc_to
          AND a2.account_id = t2.acc_from
          AND a3.account_id = t2.acc_to
        GROUP BY a1.owner_id, a3.owner_id
        ORDER BY cnt DESC, a1.owner_id ASC, a3.owner_id ASC
        LIMIT 1
        """
    )
    row = cur.fetchone()
    if row is None:
        raise RuntimeError(
            "Dataset has no 2-hop chain results — cannot construct a meaningful filtered test"
        )
    return int(row[0]), int(row[1])


# ---------------------------------------------------------------------------
# Build original (pre-rewrite) SQL for filtered chain
# ---------------------------------------------------------------------------
def build_filtered_chain_sql(src_owner: int, dest_owner: int) -> str:
    return (
        "SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3\n"
        "WHERE a1.account_id = t1.acc_from\n"
        "  AND a2.account_id = t1.acc_to\n"
        "  AND a2.account_id = t2.acc_from\n"
        "  AND a3.account_id = t2.acc_to\n"
        f"  AND a1.owner_id = {src_owner}\n"
        f"  AND a3.owner_id = {dest_owner};\n"
    )


# ---------------------------------------------------------------------------
# Run the decomposed pipeline: rewrite → sgx_app
# ---------------------------------------------------------------------------
def run_pipeline(sgx_app: str, original_sql: str, hop_csv: str, output_csv: str) -> None:
    """
    Rewrites original_sql using rewrite_chain_query, then runs sgx_app.

    sgx_app loads all CSVs from its input directory, so hop.csv is placed
    in an isolated temp directory to avoid interference from other files.

    Note: rewrite_query() prints decomposition info to stderr; this is expected.
    """
    decomposed_sql = rewrite_query(original_sql)

    # Write decomposed SQL to a temp file (sgx_app expects a file path)
    with tempfile.NamedTemporaryFile(mode="w", suffix=".sql", delete=False) as f:
        f.write(decomposed_sql)
        sql_path = f.name

    # Isolate hop.csv in its own directory so sgx_app sees only that table
    hop_dir = tempfile.mkdtemp()

    try:
        shutil.copy(hop_csv, os.path.join(hop_dir, "hop.csv"))

        result = subprocess.run(
            [sgx_app, sql_path, hop_dir, output_csv],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"sgx_app exited with code {result.returncode}.\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )
    finally:
        os.unlink(sql_path)
        shutil.rmtree(hop_dir)


# ---------------------------------------------------------------------------
# Load pipeline CSV output as list of int-valued row dicts
# ---------------------------------------------------------------------------
def load_csv_as_ints(path: str) -> list:
    rows = []
    with open(path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append({k: int(v) for k, v in row.items()})
    return rows


# ---------------------------------------------------------------------------
# Verify output schema
# ---------------------------------------------------------------------------
def check_schema(pipeline_rows: list, path: str) -> None:
    if not pipeline_rows:
        return  # Empty result — schema cannot be checked from rows alone
    actual = list(pipeline_rows[0].keys())
    if actual != EXPECTED_COLUMNS:
        raise AssertionError(
            f"Pipeline output schema mismatch.\n"
            f"  Expected: {EXPECTED_COLUMNS}\n"
            f"  Got:      {actual}\n"
            f"  (from {path})"
        )


# ---------------------------------------------------------------------------
# Sort and compare
# ---------------------------------------------------------------------------
def compare(pipeline_rows: list, sqlite_rows: list, label: str) -> bool:
    pipeline_sorted = sorted(pipeline_rows, key=lambda r: tuple(r[k] for k in SORT_KEY))
    sqlite_sorted = sorted(sqlite_rows, key=lambda r: tuple(r[k] for k in SORT_KEY))

    if len(pipeline_sorted) != len(sqlite_sorted):
        print(
            f"FAIL [{label}]: row count mismatch — "
            f"pipeline={len(pipeline_sorted)}, SQLite={len(sqlite_sorted)}"
        )
        return False

    mismatches = 0
    for i, (p, s) in enumerate(zip(pipeline_sorted, sqlite_sorted)):
        if p != s:
            mismatches += 1
            if mismatches <= 5:  # print first 5 mismatches
                print(f"FAIL [{label}]: row {i} mismatch:")
                for col in EXPECTED_COLUMNS:
                    if p.get(col) != s.get(col):
                        print(f"  {col}: pipeline={p.get(col)}  SQLite={s.get(col)}")

    if mismatches > 0:
        print(f"FAIL [{label}]: {mismatches} row(s) differ out of {len(pipeline_sorted)} total")
        return False

    print(f"PASS [{label}]: {len(pipeline_sorted)} rows match exactly")
    return True


# ---------------------------------------------------------------------------
# Run one test case end-to-end
# ---------------------------------------------------------------------------
def run_test(
    label: str,
    original_sql: str,
    sqlite_rows: list,
    sgx_app: str,
    hop_csv: str,
) -> bool:
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
        output_path = f.name

    try:
        run_pipeline(sgx_app, original_sql, hop_csv, output_path)
        pipeline_rows = load_csv_as_ints(output_path)
        check_schema(pipeline_rows, output_path)
        return compare(pipeline_rows, sqlite_rows, label)
    finally:
        os.unlink(output_path)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <data_dir> <banking_onehop_binary> <sgx_app_binary>")
        print(
            f"Example: {sys.argv[0]} input/plaintext/banking_1k "
            f"obligraph/build/banking_onehop ./sgx_app"
        )
        sys.exit(1)

    data_dir = sys.argv[1]
    onehop_binary = sys.argv[2]
    sgx_app_binary = sys.argv[3]

    for path, label in [
        (data_dir, "data_dir"),
        (onehop_binary, "banking_onehop_binary"),
        (sgx_app_binary, "sgx_app_binary"),
    ]:
        if not os.path.exists(path):
            print(f"Error: {label} '{path}' does not exist")
            sys.exit(1)

    all_passed = True

    print("[0/5] Checking dataset compatibility...")
    check_dataset(data_dir)

    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
        hop_csv = f.name

    try:
        print(f"[1/5] Running banking_onehop on {data_dir}...")
        run_onehop(onehop_binary, data_dir, hop_csv)

        print("[2/5] Building SQLite database...")
        conn = build_sqlite_db(data_dir)

        # --- Test 1: no-filter 2-hop chain ---
        print("[3/5] Test 1: no-filter 2-hop chain...")
        no_filter_sql = (
            "SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3\n"
            "WHERE a1.account_id = t1.acc_from\n"
            "  AND a2.account_id = t1.acc_to\n"
            "  AND a2.account_id = t2.acc_from\n"
            "  AND a3.account_id = t2.acc_to;\n"
        )
        sqlite_no_filter = run_sqlite_chain(conn)
        print(f"      SQLite rows: {len(sqlite_no_filter)}")
        passed = run_test("chain_no_filter", no_filter_sql, sqlite_no_filter, sgx_app_binary, hop_csv)
        all_passed = all_passed and passed

        # --- Test 2: filtered 2-hop chain (dynamically chosen filter pair) ---
        print("[4/5] Test 2: filtered 2-hop chain (auto-selected owner_id pair)...")
        src_owner, dest_owner = find_best_filter_pair(conn)
        print(f"      Selected filter: a1.owner_id={src_owner}, a3.owner_id={dest_owner}")
        filtered_sql = build_filtered_chain_sql(src_owner, dest_owner)
        sqlite_filtered = run_sqlite_chain(conn, src_owner=src_owner, dest_owner=dest_owner)
        print(f"      SQLite rows: {len(sqlite_filtered)}")
        passed = run_test("chain_filtered", filtered_sql, sqlite_filtered, sgx_app_binary, hop_csv)
        all_passed = all_passed and passed

        print("[5/5] Done.")
        sys.exit(0 if all_passed else 1)

    finally:
        os.unlink(hop_csv)


if __name__ == "__main__":
    main()
