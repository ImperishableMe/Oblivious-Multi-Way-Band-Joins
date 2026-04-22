#!/usr/bin/env python3
"""
test_large_scale_regression.py

Performance regression and correctness test for the query decomposition pipeline
on a large banking dataset (~200k accounts, ~1M transactions).

The test runs the 3-hop chain query through the decomposed pipeline and verifies
correctness against a SQLite baseline. Timing is reported for each stage so that
successive optimisation passes can be compared.

Two query variants are tested:
  1. Filtered 3-hop (default): a1.owner_id = a4.owner_id = <most-common owner>
     Full row-level correctness comparison against SQLite. Output is small (~thousands
     of rows), making comparison fast.
  2. No-filter 3-hop (--include-nofilter): No predicates.
     With 1M transactions and avg out-degree 5, the 3-hop output is ~25 million rows,
     which is impractical to compare row-by-row. Only the total row count is checked
     against a SQLite COUNT(*). This test is OFF by default because both the pipeline
     and the SQLite COUNT can be slow on very large datasets.

PIPELINE STRUCTURE (3-hop chain a1 -> t1 -> a2 -> t2 -> a3 -> t3 -> a4):
  1. banking_onehop: joins each txn with its src/dest account → hop.csv (one row per txn)
  2. rewrite_chain_query: decomposes the 3-hop SQL into a self-join on hop
  3. sgx_app: oblivious join of hop AS h1, hop AS h2, hop AS h3

Usage:
    python3 tests/test_large_scale_regression.py <data_dir> <onehop_binary> <sgx_app_binary>

Options:
    --generate <num_accounts>   Generate dataset if data_dir does not exist.
                                Uses num_accounts accounts and 5*num_accounts transactions.
                                Example: --generate 200000  (→ 200k accounts, 1M txns)
    --include-nofilter          Also run no-filter 3-hop (timing + COUNT verification).
                                WARNING: May produce a very large output file.

Examples:
    # Generate 200k-account dataset and run tests
    python3 tests/test_large_scale_regression.py input/plaintext/banking_200k \\
        obligraph/build/banking_onehop ./sgx_app --generate 200000

    # Use an existing dataset (no generation)
    python3 tests/test_large_scale_regression.py input/plaintext/banking_200k \\
        obligraph/build/banking_onehop ./sgx_app

    # Include the no-filter 3-hop test (slow for large datasets)
    python3 tests/test_large_scale_regression.py input/plaintext/banking_200k \\
        obligraph/build/banking_onehop ./sgx_app --include-nofilter
"""

import argparse
import csv
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time

# Import rewrite_chain_query from the scripts directory
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
from rewrite_chain_query import rewrite_query  # noqa: E402


# ---------------------------------------------------------------------------
# Expected output schema for 3-hop pipeline (h1.*, h2.*, h3.*)
# ---------------------------------------------------------------------------
# Column order within each hop matches the oneHop output schema: txn columns
# first (in CSV order), then account_src_*, then account_dest_*.
_HOP_COLUMNS = [
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

EXPECTED_COLUMNS = (
    [f"h1.{c}" for c in _HOP_COLUMNS]
    + [f"h2.{c}" for c in _HOP_COLUMNS]
    + [f"h3.{c}" for c in _HOP_COLUMNS]
)

# Sort key: (h1.txn_id, h2.txn_id, h3.txn_id) uniquely identifies each 3-hop chain
# because txn_id is unique per transaction.
SORT_KEY = ("h1.txn_id", "h2.txn_id", "h3.txn_id")


# ---------------------------------------------------------------------------
# Dataset generation
# ---------------------------------------------------------------------------
def generate_dataset(data_dir: str, num_accounts: int) -> None:
    print(f"Generating dataset: {num_accounts:,} accounts, {5 * num_accounts:,} transactions...")
    script = os.path.join(os.path.dirname(__file__), "..", "scripts", "generate_banking_scaled.py")
    result = subprocess.run(
        [sys.executable, script, str(num_accounts), data_dir, "--seed", "42"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Dataset generation failed:\n{result.stderr}")
    print(result.stdout.strip())


# ---------------------------------------------------------------------------
# Dataset compatibility check (obligraph requires all IDs > 0)
# ---------------------------------------------------------------------------
def check_dataset(data_dir: str) -> None:
    account_path = os.path.join(data_dir, "account.csv")
    txn_path = os.path.join(data_dir, "txn.csv")
    errors = []

    with open(account_path) as f:
        for i, row in enumerate(csv.DictReader(f), start=1):
            if int(row["account_id"]) <= 0:
                errors.append(
                    f"account.csv row {i}: account_id={row['account_id']} is non-positive. "
                    f"Sentinel rows (account_id=-10000) must be removed before passing to obligraph."
                )
            if i > 10 and errors:
                break  # stop early after first batch of errors

    with open(txn_path) as f:
        for i, row in enumerate(csv.DictReader(f), start=1):
            for col in ("acc_from", "acc_to"):
                if int(row[col]) <= 0:
                    errors.append(
                        f"txn.csv row {i}: {col}={row[col]} is non-positive. "
                        f"Sentinel rows must be removed before passing to obligraph."
                    )
                    break
            if i > 10 and errors:
                break

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
# Run banking_onehop binary
# ---------------------------------------------------------------------------
def run_onehop(binary: str, data_dir: str, output_path: str) -> float:
    """Run oneHop and return wall-clock time in seconds."""
    t0 = time.perf_counter()
    result = subprocess.run(
        [binary, data_dir, output_path],
        capture_output=True,
        text=True,
    )
    elapsed = time.perf_counter() - t0
    if result.returncode != 0:
        raise RuntimeError(
            f"banking_onehop exited with code {result.returncode}.\n"
            f"stderr:\n{result.stderr}"
        )
    return elapsed


# ---------------------------------------------------------------------------
# Build in-memory SQLite database with indexes for fast joins and counts
# ---------------------------------------------------------------------------
def build_sqlite_db(data_dir: str) -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    cur = conn.cursor()

    cur.execute(
        "CREATE TABLE account "
        "(account_id INTEGER PRIMARY KEY, balance INTEGER, owner_id INTEGER)"
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

    # Indexes for efficient multi-hop joins
    cur.execute("CREATE INDEX idx_txn_from ON txn(acc_from)")
    cur.execute("CREATE INDEX idx_txn_to   ON txn(acc_to)")
    cur.execute("CREATE INDEX idx_acc_owner ON account(owner_id)")
    conn.commit()
    return conn


# ---------------------------------------------------------------------------
# Find the most common owner_id (used as the deterministic filter target)
# ---------------------------------------------------------------------------
def find_most_common_owner(conn: sqlite3.Connection) -> int:
    cur = conn.cursor()
    cur.execute(
        "SELECT owner_id, count(*) AS cnt FROM account "
        "GROUP BY owner_id ORDER BY cnt DESC, owner_id ASC LIMIT 1"
    )
    return int(cur.fetchone()[0])


# ---------------------------------------------------------------------------
# SQLite 3-hop chain query (full rows)
# ---------------------------------------------------------------------------
# Column aliases match the sgx_app output schema exactly (h1.*, h2.*, h3.*).
# a2 is shared between h1.account_dest_* and h2.account_src_* (same node),
# and a3 is shared between h2.account_dest_* and h3.account_src_*.
_CHAIN3_ROWS_SQL = """\
SELECT
  t1.txn_id                 AS "h1.txn_id",
  t1.acc_from               AS "h1.acc_from",
  t1.acc_to                 AS "h1.acc_to",
  t1.amount                 AS "h1.amount",
  t1.txn_time               AS "h1.txn_time",
  a1.account_id             AS "h1.account_src_account_id",
  a1.balance                AS "h1.account_src_balance",
  a1.owner_id               AS "h1.account_src_owner_id",
  a2.account_id             AS "h1.account_dest_account_id",
  a2.balance                AS "h1.account_dest_balance",
  a2.owner_id               AS "h1.account_dest_owner_id",
  t2.txn_id                 AS "h2.txn_id",
  t2.acc_from               AS "h2.acc_from",
  t2.acc_to                 AS "h2.acc_to",
  t2.amount                 AS "h2.amount",
  t2.txn_time               AS "h2.txn_time",
  a2.account_id             AS "h2.account_src_account_id",
  a2.balance                AS "h2.account_src_balance",
  a2.owner_id               AS "h2.account_src_owner_id",
  a3.account_id             AS "h2.account_dest_account_id",
  a3.balance                AS "h2.account_dest_balance",
  a3.owner_id               AS "h2.account_dest_owner_id",
  t3.txn_id                 AS "h3.txn_id",
  t3.acc_from               AS "h3.acc_from",
  t3.acc_to                 AS "h3.acc_to",
  t3.amount                 AS "h3.amount",
  t3.txn_time               AS "h3.txn_time",
  a3.account_id             AS "h3.account_src_account_id",
  a3.balance                AS "h3.account_src_balance",
  a3.owner_id               AS "h3.account_src_owner_id",
  a4.account_id             AS "h3.account_dest_account_id",
  a4.balance                AS "h3.account_dest_balance",
  a4.owner_id               AS "h3.account_dest_owner_id"
FROM txn t1, txn t2, txn t3, account a1, account a2, account a3, account a4
WHERE a1.account_id = t1.acc_from
  AND a2.account_id = t1.acc_to
  AND a2.account_id = t2.acc_from
  AND a3.account_id = t2.acc_to
  AND a3.account_id = t3.acc_from
  AND a4.account_id = t3.acc_to
  {filter_clause}
"""

_CHAIN3_COUNT_SQL = """\
SELECT count(*)
FROM txn t1, txn t2, txn t3, account a1, account a2, account a3, account a4
WHERE a1.account_id = t1.acc_from
  AND a2.account_id = t1.acc_to
  AND a2.account_id = t2.acc_from
  AND a3.account_id = t2.acc_to
  AND a3.account_id = t3.acc_from
  AND a4.account_id = t3.acc_to
  {filter_clause}
"""


def sqlite_rows_3hop(
    conn: sqlite3.Connection,
    src_owner: int = None,
    dest_owner: int = None,
) -> list:
    parts = []
    if src_owner is not None:
        parts.append(f"AND a1.owner_id = {src_owner}")
    if dest_owner is not None:
        parts.append(f"AND a4.owner_id = {dest_owner}")
    cur = conn.cursor()
    cur.execute(_CHAIN3_ROWS_SQL.format(filter_clause=" ".join(parts)))
    cols = [d[0] for d in cur.description]
    return [dict(zip(cols, [int(v) for v in row])) for row in cur.fetchall()]


def sqlite_count_3hop(
    conn: sqlite3.Connection,
    src_owner: int = None,
    dest_owner: int = None,
) -> int:
    parts = []
    if src_owner is not None:
        parts.append(f"AND a1.owner_id = {src_owner}")
    if dest_owner is not None:
        parts.append(f"AND a4.owner_id = {dest_owner}")
    cur = conn.cursor()
    cur.execute(_CHAIN3_COUNT_SQL.format(filter_clause=" ".join(parts)))
    return int(cur.fetchone()[0])


# ---------------------------------------------------------------------------
# Build original-SQL strings for the 3-hop chain
# ---------------------------------------------------------------------------
def build_chain3_sql_nofilter() -> str:
    return (
        "SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2,"
        " account AS a3, txn AS t3, account AS a4\n"
        "WHERE a1.account_id = t1.acc_from\n"
        "  AND a2.account_id = t1.acc_to\n"
        "  AND a2.account_id = t2.acc_from\n"
        "  AND a3.account_id = t2.acc_to\n"
        "  AND a3.account_id = t3.acc_from\n"
        "  AND a4.account_id = t3.acc_to;\n"
    )


def build_chain3_sql_filtered(src_owner: int, dest_owner: int) -> str:
    return (
        "SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2,"
        " account AS a3, txn AS t3, account AS a4\n"
        "WHERE a1.account_id = t1.acc_from\n"
        "  AND a2.account_id = t1.acc_to\n"
        "  AND a2.account_id = t2.acc_from\n"
        "  AND a3.account_id = t2.acc_to\n"
        "  AND a3.account_id = t3.acc_from\n"
        "  AND a4.account_id = t3.acc_to\n"
        f"  AND a1.owner_id = {src_owner}\n"
        f"  AND a4.owner_id = {dest_owner};\n"
    )


# ---------------------------------------------------------------------------
# Run the decomposed pipeline (rewrite → sgx_app) and return elapsed seconds
# ---------------------------------------------------------------------------
def run_pipeline(sgx_app: str, original_sql: str, hop_csv: str, output_csv: str) -> float:
    decomposed_sql = rewrite_query(original_sql)

    with tempfile.NamedTemporaryFile(mode="w", suffix=".sql", delete=False) as f:
        f.write(decomposed_sql)
        sql_path = f.name

    # sgx_app loads all CSVs in the directory it is pointed at, so isolate hop.csv
    hop_dir = tempfile.mkdtemp()
    try:
        shutil.copy(hop_csv, os.path.join(hop_dir, "hop.csv"))

        t0 = time.perf_counter()
        result = subprocess.run(
            [sgx_app, sql_path, hop_dir, output_csv],
            capture_output=True,
            text=True,
        )
        elapsed = time.perf_counter() - t0

        if result.returncode != 0:
            raise RuntimeError(
                f"sgx_app exited with code {result.returncode}.\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )
        return elapsed
    finally:
        os.unlink(sql_path)
        shutil.rmtree(hop_dir)


# ---------------------------------------------------------------------------
# Count rows in a CSV file without loading it entirely into memory
# ---------------------------------------------------------------------------
def count_csv_rows(path: str) -> int:
    count = 0
    with open(path) as f:
        for i, _ in enumerate(f):
            if i > 0:  # skip header
                count += 1
    return count


# ---------------------------------------------------------------------------
# Load a CSV file as a list of int-valued row dicts
# ---------------------------------------------------------------------------
def load_csv_as_ints(path: str) -> list:
    rows = []
    with open(path) as f:
        for row in csv.DictReader(f):
            rows.append({k: int(v) for k, v in row.items()})
    return rows


# ---------------------------------------------------------------------------
# Verify output schema
# ---------------------------------------------------------------------------
def check_schema(rows: list, path: str) -> None:
    if not rows:
        return
    actual = list(rows[0].keys())
    if actual != EXPECTED_COLUMNS:
        raise AssertionError(
            f"Pipeline output schema mismatch.\n"
            f"  Expected: {EXPECTED_COLUMNS}\n"
            f"  Got:      {actual}\n"
            f"  (from {path})"
        )


# ---------------------------------------------------------------------------
# Sort and compare two row sets
# ---------------------------------------------------------------------------
def compare_rows(pipeline_rows: list, sqlite_rows: list, label: str) -> bool:
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
            if mismatches <= 5:
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
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    parser = argparse.ArgumentParser(
        description="Large-scale performance regression test for the 3-hop pipeline"
    )
    parser.add_argument("data_dir", help="Banking dataset directory (account.csv + txn.csv)")
    parser.add_argument("onehop_binary", help="Path to banking_onehop binary")
    parser.add_argument("sgx_app_binary", help="Path to sgx_app binary")
    parser.add_argument(
        "--generate",
        type=int,
        metavar="NUM_ACCOUNTS",
        help=(
            "Generate dataset with NUM_ACCOUNTS accounts and 5×NUM_ACCOUNTS transactions "
            "if data_dir does not exist. Example: --generate 200000"
        ),
    )
    parser.add_argument(
        "--include-nofilter",
        action="store_true",
        help=(
            "Also run the no-filter 3-hop test (row COUNT comparison only). "
            "WARNING: With 1M transactions the pipeline may produce ~25M output rows "
            "and both the pipeline run and SQLite COUNT(*) can be slow."
        ),
    )
    args = parser.parse_args()

    data_dir = args.data_dir
    onehop_binary = args.onehop_binary
    sgx_app_binary = args.sgx_app_binary

    # --- Generate dataset if requested ---
    if args.generate:
        if not os.path.isdir(data_dir):
            generate_dataset(data_dir, args.generate)
        else:
            print(f"Dataset directory '{data_dir}' already exists, skipping generation.")

    # --- Validate paths ---
    for path, label in [
        (data_dir, "data_dir"),
        (onehop_binary, "onehop_binary"),
        (sgx_app_binary, "sgx_app_binary"),
    ]:
        if not os.path.exists(path):
            print(f"Error: {label} '{path}' does not exist")
            sys.exit(1)

    all_passed = True
    timings = {}  # stage → (elapsed_seconds, row_count)

    # --- Dataset info ---
    print("[0/N] Checking dataset compatibility...")
    check_dataset(data_dir)

    with open(os.path.join(data_dir, "account.csv")) as f:
        num_accounts = sum(1 for _ in f) - 1
    with open(os.path.join(data_dir, "txn.csv")) as f:
        num_txns = sum(1 for _ in f) - 1
    print(f"      Dataset: {num_accounts:,} accounts, {num_txns:,} transactions")

    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
        hop_csv = f.name

    try:
        # --- Step 1: Run oneHop ---
        print(f"\n[1/N] Running banking_onehop on {data_dir}...")
        onehop_elapsed = run_onehop(onehop_binary, data_dir, hop_csv)
        hop_rows = count_csv_rows(hop_csv)
        timings["oneHop"] = (onehop_elapsed, hop_rows)
        print(f"      Done in {onehop_elapsed:.2f}s  ({hop_rows:,} hop rows)")
        print(f"TIMING: oneHop={onehop_elapsed:.3f}s rows={hop_rows}")

        # --- Step 2: Load SQLite ---
        print("\n[2/N] Loading data into SQLite (with indexes)...")
        t0 = time.perf_counter()
        conn = build_sqlite_db(data_dir)
        sqlite_load_elapsed = time.perf_counter() - t0
        print(f"      Done in {sqlite_load_elapsed:.2f}s")

        # --- Determine filter ---
        src_owner = find_most_common_owner(conn)
        dest_owner = src_owner  # same owner on both ends keeps output small
        print(f"      Filter: a1.owner_id = a4.owner_id = {src_owner} (most common owner)")

        # --- Step 3: Filtered 3-hop (correctness + timing) ---
        print(
            f"\n[3/N] Test 1 — Filtered 3-hop chain "
            f"(a1.owner_id = a4.owner_id = {src_owner})..."
        )
        filtered_sql = build_chain3_sql_filtered(src_owner, dest_owner)

        with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
            filtered_output = f.name
        try:
            sgx_elapsed = run_pipeline(sgx_app_binary, filtered_sql, hop_csv, filtered_output)
            pipeline_rows = load_csv_as_ints(filtered_output)
            timings["sgx_filtered_3hop"] = (sgx_elapsed, len(pipeline_rows))
            print(f"      Pipeline: {sgx_elapsed:.2f}s  ({len(pipeline_rows):,} rows)")
            print(f"TIMING: sgx_filtered_3hop={sgx_elapsed:.3f}s rows={len(pipeline_rows)}")

            check_schema(pipeline_rows, filtered_output)

            print("      Running SQLite filtered baseline...")
            t0 = time.perf_counter()
            sqlite_rows = sqlite_rows_3hop(conn, src_owner=src_owner, dest_owner=dest_owner)
            sqlite_elapsed = time.perf_counter() - t0
            print(f"      SQLite:   {sqlite_elapsed:.2f}s  ({len(sqlite_rows):,} rows)")

            passed = compare_rows(pipeline_rows, sqlite_rows, "filtered_3hop")
            all_passed = all_passed and passed
        finally:
            os.unlink(filtered_output)

        # --- Step 4 (optional): No-filter 3-hop (count comparison) ---
        if args.include_nofilter:
            print(
                "\n[4/N] Test 2 — No-filter 3-hop chain "
                "(COUNT comparison only; may be slow on large datasets)..."
            )
            nofilter_sql = build_chain3_sql_nofilter()

            with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
                nofilter_output = f.name
            try:
                sgx_elapsed = run_pipeline(sgx_app_binary, nofilter_sql, hop_csv, nofilter_output)
                output_count = count_csv_rows(nofilter_output)
                timings["sgx_nofilter_3hop"] = (sgx_elapsed, output_count)
                print(f"      Pipeline: {sgx_elapsed:.2f}s  ({output_count:,} rows)")
                print(f"TIMING: sgx_nofilter_3hop={sgx_elapsed:.3f}s rows={output_count}")

                print("      Running SQLite no-filter COUNT(*) (may take a while)...")
                t0 = time.perf_counter()
                sqlite_count = sqlite_count_3hop(conn)
                sqlite_elapsed = time.perf_counter() - t0
                print(f"      SQLite COUNT(*): {sqlite_count:,}  ({sqlite_elapsed:.2f}s)")

                if output_count == sqlite_count:
                    print(f"PASS [nofilter_3hop]: row count matches ({output_count:,})")
                else:
                    print(
                        f"FAIL [nofilter_3hop]: count mismatch — "
                        f"pipeline={output_count:,}, SQLite={sqlite_count:,}"
                    )
                    all_passed = False
            finally:
                os.unlink(nofilter_output)

    finally:
        os.unlink(hop_csv)

    # --- Timing summary ---
    print()
    print("=" * 62)
    print("TIMING SUMMARY")
    print("=" * 62)
    print(f"{'Stage':<32} {'Time (s)':>10}  {'Rows':>15}")
    print("-" * 62)
    for stage, (elapsed, rows) in timings.items():
        print(f"{stage:<32} {elapsed:>10.3f}  {rows:>15,}")
    print("=" * 62)

    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
