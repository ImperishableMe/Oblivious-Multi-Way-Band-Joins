#!/usr/bin/env python3
"""
test_onehop_correctness_scaled.py

Large-scale correctness test for the oneHop join. Same semantics as
test_onehop_correctness.py but designed to handle datasets with tens of
millions of rows (where the small-scale test's per-row INSERT + Python
dict sort would be prohibitively slow).

Strategy:
  1. Run banking_onehop to produce onehop_out.csv.
  2. Load account.csv, txn.csv, and onehop_out.csv into a disk-backed
     SQLite DB using batched executemany inside a single transaction.
  3. Run the equivalent SQL join and materialise its result into another
     table.
  4. Compare row count, then use SQL EXCEPT to find any rows present in
     one side but missing from the other (bounded to first 5 diffs).

Usage:
    python3 tests/test_onehop_correctness_scaled.py <data_dir> <banking_onehop_binary>
"""

import csv
import os
import sqlite3
import subprocess
import sys
import tempfile
import time

BATCH_SIZE = 200_000

EXPECTED_COLUMNS = [
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


def run_onehop(binary: str, data_dir: str, output_path: str) -> float:
    t0 = time.time()
    result = subprocess.run(
        [binary, data_dir, output_path],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"banking_onehop exited {result.returncode}.\nstderr:\n{result.stderr}"
        )
    return time.time() - t0


def bulk_load_csv(conn: sqlite3.Connection, table: str, csv_path: str,
                  columns: list, types: list) -> int:
    cur = conn.cursor()
    col_defs = ", ".join(f"{c} {t}" for c, t in zip(columns, types))
    cur.execute(f"DROP TABLE IF EXISTS {table}")
    cur.execute(f"CREATE TABLE {table} ({col_defs})")
    placeholders = ",".join("?" * len(columns))
    insert_sql = f"INSERT INTO {table} VALUES ({placeholders})"

    total = 0
    with open(csv_path, "r", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        if [h.strip() for h in header] != columns:
            raise RuntimeError(
                f"{csv_path} header mismatch. Expected {columns}, got {header}"
            )

        batch = []
        for row in reader:
            batch.append([int(v) for v in row])
            if len(batch) >= BATCH_SIZE:
                cur.executemany(insert_sql, batch)
                total += len(batch)
                batch.clear()
        if batch:
            cur.executemany(insert_sql, batch)
            total += len(batch)

    conn.commit()
    return total


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <data_dir> <banking_onehop_binary>")
        sys.exit(1)

    data_dir = sys.argv[1]
    binary = sys.argv[2]

    account_csv = os.path.join(data_dir, "account.csv")
    txn_csv = os.path.join(data_dir, "txn.csv")
    if not os.path.isfile(account_csv) or not os.path.isfile(txn_csv):
        print(f"Error: {account_csv} or {txn_csv} missing")
        sys.exit(1)
    if not os.path.isfile(binary):
        print(f"Error: binary '{binary}' does not exist")
        sys.exit(1)

    tmpdir = tempfile.mkdtemp(prefix="onehop_scaled_")
    onehop_out = os.path.join(tmpdir, "onehop_out.csv")
    db_path = os.path.join(tmpdir, "compare.db")
    print(f"[tmpdir] {tmpdir}")

    try:
        # ---------- [1/5] run banking_onehop ----------
        print(f"[1/5] Running banking_onehop on {data_dir} ...")
        t_oh = run_onehop(binary, data_dir, onehop_out)
        oh_size = os.path.getsize(onehop_out)
        print(f"      oneHop done in {t_oh:.1f}s, output {oh_size/1e6:.1f} MB")

        conn = sqlite3.connect(db_path)
        conn.execute("PRAGMA journal_mode = OFF")
        conn.execute("PRAGMA synchronous  = OFF")
        conn.execute("PRAGMA temp_store   = MEMORY")
        conn.execute("PRAGMA cache_size   = -2000000")  # 2 GiB page cache

        # ---------- [2/5] load CSVs into SQLite ----------
        print("[2/5] Loading account.csv, txn.csv, onehop_out.csv into SQLite ...")
        t0 = time.time()
        n_acc = bulk_load_csv(
            conn, "account", account_csv,
            ["account_id", "balance", "owner_id"],
            ["INTEGER PRIMARY KEY", "INTEGER", "INTEGER"],
        )
        print(f"      account loaded: {n_acc:,} rows ({time.time()-t0:.1f}s)")

        t0 = time.time()
        n_txn = bulk_load_csv(
            conn, "txn", txn_csv,
            ["txn_id", "acc_from", "acc_to", "amount", "txn_time"],
            ["INTEGER PRIMARY KEY", "INTEGER", "INTEGER", "INTEGER", "INTEGER"],
        )
        print(f"      txn loaded:     {n_txn:,} rows ({time.time()-t0:.1f}s)")

        t0 = time.time()
        n_oh = bulk_load_csv(
            conn, "onehop", onehop_out,
            EXPECTED_COLUMNS,
            ["INTEGER PRIMARY KEY"] + ["INTEGER"] * (len(EXPECTED_COLUMNS) - 1),
        )
        print(f"      onehop loaded:  {n_oh:,} rows ({time.time()-t0:.1f}s)")

        # ---------- [3/5] materialise SQLite baseline ----------
        print("[3/5] Building SQLite baseline join ...")
        t0 = time.time()
        conn.execute("DROP TABLE IF EXISTS expected")
        conn.execute(f"""
            CREATE TABLE expected AS
            SELECT
                t.txn_id                          AS txn_id,
                t.acc_from                        AS acc_from,
                t.acc_to                          AS acc_to,
                t.amount                          AS amount,
                t.txn_time                        AS txn_time,
                a_src.account_id                  AS account_src_account_id,
                a_src.balance                     AS account_src_balance,
                a_src.owner_id                    AS account_src_owner_id,
                a_dst.account_id                  AS account_dest_account_id,
                a_dst.balance                     AS account_dest_balance,
                a_dst.owner_id                    AS account_dest_owner_id
            FROM txn t
            JOIN account a_src ON a_src.account_id = t.acc_from
            JOIN account a_dst ON a_dst.account_id = t.acc_to
        """)
        conn.commit()
        n_exp = conn.execute("SELECT COUNT(*) FROM expected").fetchone()[0]
        print(f"      expected rows: {n_exp:,} ({time.time()-t0:.1f}s)")

        # ---------- [4/5] row-count and EXCEPT diff ----------
        print("[4/5] Comparing ...")
        if n_oh != n_exp:
            print(f"FAIL: row count mismatch — oneHop={n_oh:,}, SQLite={n_exp:,}")
            sys.exit(1)

        t0 = time.time()
        cols = ", ".join(EXPECTED_COLUMNS)
        missing_from_onehop = conn.execute(f"""
            SELECT {cols} FROM expected
            EXCEPT
            SELECT {cols} FROM onehop
            LIMIT 5
        """).fetchall()
        extra_in_onehop = conn.execute(f"""
            SELECT {cols} FROM onehop
            EXCEPT
            SELECT {cols} FROM expected
            LIMIT 5
        """).fetchall()
        print(f"      EXCEPT diff ({time.time()-t0:.1f}s)")

        if missing_from_onehop or extra_in_onehop:
            print("FAIL: row set differs between oneHop and SQLite.")
            if missing_from_onehop:
                print(f"  missing from oneHop (first {len(missing_from_onehop)}):")
                for r in missing_from_onehop:
                    print(f"    {dict(zip(EXPECTED_COLUMNS, r))}")
            if extra_in_onehop:
                print(f"  extra in oneHop (first {len(extra_in_onehop)}):")
                for r in extra_in_onehop:
                    print(f"    {dict(zip(EXPECTED_COLUMNS, r))}")
            sys.exit(1)

        # ---------- [5/5] done ----------
        print(f"[5/5] PASS — {n_oh:,} rows match exactly.")
        sys.exit(0)

    finally:
        try:
            os.unlink(onehop_out)
        except FileNotFoundError:
            pass
        try:
            os.unlink(db_path)
        except FileNotFoundError:
            pass
        try:
            os.rmdir(tmpdir)
        except OSError:
            pass


if __name__ == "__main__":
    main()
