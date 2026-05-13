#!/usr/bin/env python3
"""
E1 chain-correctness gate.

Compares the output of three systems on the five Banking chain queries
(banking_1hop.sql ... banking_5hop.sql) for a given dataset:

  1. SQLite (gold)
  2. Full MWJ   - sgx_app run directly on the chain query
  3. NebulaDB
       1-hop  : banking_onehop alone (the one-hop binary IS the join)
       >= 2-hop : banking_onehop -> rewrite_chain_query -> sgx_app

Comparison key per row: the tuple of txn_id values in hop order. This is
schema-agnostic (each system labels its txn_id columns differently) and
uniquely identifies a chain since txn_id is unique per transaction.

Usage:
  python3 tests/test_e1_chain_correctness.py <data_dir> <banking_onehop_bin> <sgx_app_bin>
"""

import csv
import os
import sqlite3
import subprocess
import sys
import tempfile

QUERIES = ["banking_1hop", "banking_2hop", "banking_3hop", "banking_4hop", "banking_5hop"]
QUERY_DIR = "input/queries"
REWRITER = "scripts/rewrite_chain_query.py"


def is_txn_id_col(name: str) -> bool:
    return name == "txn_id" or name.endswith(".txn_id")


def hop_count(query_name: str) -> int:
    # "banking_3hop" -> 3
    return int(query_name.split("_")[1].rstrip("hop"))


def sqlite_gold(data_dir: str, query_path: str):
    conn = sqlite3.connect(":memory:")
    c = conn.cursor()
    for tbl, cols in [
        ("account", ["account_id", "balance", "owner_id"]),
        ("txn", ["txn_id", "acc_from", "acc_to", "amount", "txn_time"]),
    ]:
        c.execute(f"CREATE TABLE {tbl} ({','.join(f'{n} INTEGER' for n in cols)})")
        with open(f"{data_dir}/{tbl}.csv") as f:
            r = csv.reader(f); next(r)
            placeholders = ",".join("?" * len(cols))
            c.executemany(f"INSERT INTO {tbl} VALUES ({placeholders})", r)
    conn.commit()
    sql = open(query_path).read().replace(";", "")
    rows = c.execute(sql).fetchall()
    cols = [d[0] for d in c.description]
    positions = [i for i, n in enumerate(cols) if is_txn_id_col(n)]
    return sorted(tuple(row[i] for i in positions) for row in rows)


def parse_txn_tuples(csv_path: str):
    with open(csv_path) as f:
        r = csv.reader(f)
        header = next(r)
        positions = [i for i, n in enumerate(header) if is_txn_id_col(n)]
        return sorted(tuple(int(row[i]) for i in positions) for row in r)


def run_full_mwj(query_path: str, data_dir: str, sgx_app: str):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "out.csv")
        subprocess.run([sgx_app, query_path, data_dir, out],
                       check=True, capture_output=True, text=True)
        return parse_txn_tuples(out)


def run_one_hop_only(data_dir: str, banking_onehop: str):
    """NebulaDB on a 1-hop query: just the one-hop binary; its output IS the result."""
    with tempfile.TemporaryDirectory() as tmp:
        hop_csv = os.path.join(tmp, "hop.csv")
        subprocess.run([banking_onehop, data_dir, hop_csv],
                       check=True, capture_output=True, text=True)
        result = parse_txn_tuples(hop_csv)
        # The one-hop binary emits all txns, unfiltered. The chain query's
        # `a1.account_id = X` filter has to be applied here for 1-hop parity
        # with SQLite (Full MWJ applies it inside the query itself).
        # In the >=2-hop pipeline, the filter is applied by the rewritten
        # query running on the hop table.
        return result


def run_nebuladb_pipeline(query_path: str, data_dir: str, banking_onehop: str, sgx_app: str):
    """NebulaDB for chains with >= 2 hops: one-hop -> rewrite -> MWJ."""
    with tempfile.TemporaryDirectory() as tmp:
        mwj_dir = os.path.join(tmp, "mwj_data")
        os.makedirs(mwj_dir)
        subprocess.run([banking_onehop, data_dir, os.path.join(mwj_dir, "hop.csv")],
                       check=True, capture_output=True, text=True)
        decomposed = os.path.join(tmp, "decomposed.sql")
        subprocess.run(["python3", REWRITER, query_path, decomposed],
                       check=True, capture_output=True, text=True)
        out = os.path.join(tmp, "out.csv")
        subprocess.run([sgx_app, decomposed, mwj_dir, out],
                       check=True, capture_output=True, text=True)
        return parse_txn_tuples(out)


def filter_onehop_to_starting_account(hop_tuples, data_dir: str, starting_acc: int):
    """The one-hop binary emits ALL txns. For 1-hop correctness we need only
    the rows where acc_from == starting_acc. Re-read hop.csv? Cheaper: filter
    using the txn table directly since acc_from/acc_to are also there.
    But our parse_txn_tuples only kept txn_id. So instead, re-implement the
    one-hop run inline here to apply the filter post-hoc."""
    raise NotImplementedError  # see run_one_hop_for_query below


def run_one_hop_for_query(query_path: str, data_dir: str, banking_onehop: str):
    """1-hop NebulaDB: run one-hop, then apply the query's `a1.account_id = X`
    filter to its output. We treat hop.csv as the 1-hop join result of
    (account ⋈ txn ⋈ account) and filter by account_src_account_id."""
    # extract the starting account id from the query text
    sql = open(query_path).read()
    # simple regex: `a1.account_id = N`
    import re
    m = re.search(r"a1\.account_id\s*=\s*(\d+)", sql)
    if not m:
        raise RuntimeError(f"could not find starting account filter in {query_path}")
    start_id = int(m.group(1))

    with tempfile.TemporaryDirectory() as tmp:
        hop_csv = os.path.join(tmp, "hop.csv")
        subprocess.run([banking_onehop, data_dir, hop_csv],
                       check=True, capture_output=True, text=True)
        # parse + filter by account_src_account_id == start_id
        with open(hop_csv) as f:
            r = csv.reader(f)
            header = next(r)
            src_col = header.index("account_src_account_id")
            txn_col = header.index("txn_id")
            return sorted(
                (int(row[txn_col]),)
                for row in r if int(row[src_col]) == start_id
            )


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <data_dir> <banking_onehop_bin> <sgx_app_bin>",
              file=sys.stderr)
        sys.exit(2)
    data_dir, banking_onehop, sgx_app = sys.argv[1:4]

    print(f"Data: {data_dir}")
    print(f"Queries: {', '.join(QUERIES)}")
    print()
    print(f"{'query':<14} {'gold':>6} {'mwj':>6} {'neb':>6}  result")
    print("-" * 50)

    failures = []
    for q in QUERIES:
        qpath = os.path.join(QUERY_DIR, f"{q}.sql")
        gold = sqlite_gold(data_dir, qpath)

        try:
            mwj = run_full_mwj(qpath, data_dir, sgx_app)
            mwj_ok = mwj == gold
        except subprocess.CalledProcessError as e:
            mwj, mwj_ok = None, False
            failures.append((q, "FullMWJ", f"crashed: {e.stderr[:200]}"))

        try:
            if hop_count(q) == 1:
                neb = run_one_hop_for_query(qpath, data_dir, banking_onehop)
            else:
                neb = run_nebuladb_pipeline(qpath, data_dir, banking_onehop, sgx_app)
            neb_ok = neb == gold
        except subprocess.CalledProcessError as e:
            neb, neb_ok = None, False
            failures.append((q, "NebulaDB", f"crashed: {e.stderr[:200]}"))

        status_parts = []
        if mwj is not None and not mwj_ok:
            failures.append((q, "FullMWJ", f"{len(mwj)} rows vs gold {len(gold)} -- mismatch"))
            status_parts.append("MWJ_MISMATCH")
        if neb is not None and not neb_ok:
            failures.append((q, "NebulaDB", f"{len(neb)} rows vs gold {len(gold)} -- mismatch"))
            status_parts.append("NEB_MISMATCH")
        status = "OK" if mwj_ok and neb_ok else (" ".join(status_parts) or "ERR")

        print(f"{q:<14} {len(gold):>6} "
              f"{('-' if mwj is None else len(mwj)):>6} "
              f"{('-' if neb is None else len(neb)):>6}  {status}")

    print()
    if failures:
        print(f"FAILURES ({len(failures)}):")
        for qn, sysn, msg in failures:
            print(f"  {qn} / {sysn}: {msg}")
        sys.exit(1)
    print("All correctness checks passed.")
    sys.exit(0)


if __name__ == "__main__":
    main()
