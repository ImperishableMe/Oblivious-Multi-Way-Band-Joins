#!/usr/bin/env python3
"""
E4 shape-correctness gate.

Compares system output against SQLite (gold) on the six E4 queries — the
chain context queries (aml_2hop, aml_3hop, aml_4hop) and the size-matched
4-edge shapes (aml_fanin, aml_fanout, aml_tree) — for a given AML dataset:

  1. SQLite (gold)
  2. NebulaDB/Graphite: one-hop binary -> rewrite_chain_query.py -> sgx_app
     on the decomposed query over the hop table. The rewriter's BFS
     decomposition handles branching topologies, so the same pipeline serves
     chains, fan-in, fan-out, and tree.
  3. (--with-full-mwj) sgx_app directly on the original filtered query.
     Off by default: it re-validates the MWJ engine, not the E4 pipeline,
     and costs minutes per shape.

Comparison key per row: the tuple of txn_id values in hop order (t1..t4).
The rewriter's BFS visits transactions in t-number order for all E4 shapes,
so gold's FROM-order txn_id columns and the decomposed query's h1..h4
txn_id columns line up. Table schemas are read from the CSV headers, so the
test works with both full and slim txn tables.

Usage:
  python3 tests/test_e4_shape_correctness.py <data_dir> <aml_onehop_bin> <sgx_app_bin>
  python3 tests/test_e4_shape_correctness.py <data_dir> <onehop> <sgx_app> --queries aml_fanin,aml_tree
"""

import argparse
import csv
import os
import re
import sqlite3
import subprocess
import sys
import tempfile

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUERY_DIR = os.path.join(PROJECT_DIR, "input", "queries")
REWRITER = os.path.join(PROJECT_DIR, "scripts", "rewrite_chain_query.py")

DEFAULT_QUERIES = ["aml_2hop", "aml_3hop", "aml_4hop",
                   "aml_fanin", "aml_fanout", "aml_tree"]


def is_txn_id_col(name: str) -> bool:
    return name == "txn_id" or name.endswith(".txn_id")


def txn_aliases_in_from_order(sql: str):
    """['t1', 't2', ...] in FROM-clause order."""
    m = re.search(r"FROM\s+(.+?)\s+WHERE", sql, re.IGNORECASE | re.DOTALL)
    if not m:
        raise RuntimeError("could not parse FROM clause")
    return [alias for tbl, alias in
            re.findall(r"(\w+)\s+AS\s+(\w+)", m.group(1), re.IGNORECASE)
            if tbl.lower() == "txn"]


def sqlite_gold(data_dir: str, query_path: str):
    """Run the query in SQLite, selecting only the txn_id columns (in
    FROM-clause alias order) to keep memory bounded on multi-million-row
    outputs. Returns a sorted list of txn_id tuples."""
    conn = sqlite3.connect(":memory:")
    c = conn.cursor()
    for tbl in ["account", "txn"]:
        with open(os.path.join(data_dir, f"{tbl}.csv")) as f:
            r = csv.reader(f)
            header = next(r)
            c.execute(f"CREATE TABLE {tbl} ({','.join(f'{n} INTEGER' for n in header)})")
            ph = ",".join("?" * len(header))
            c.executemany(f"INSERT INTO {tbl} VALUES ({ph})", r)
    c.execute("CREATE INDEX idx_txn_from ON txn(acc_from)")
    c.execute("CREATE INDEX idx_txn_to ON txn(acc_to)")
    conn.commit()

    sql = open(query_path).read().replace(";", "")
    t_aliases = txn_aliases_in_from_order(sql)
    projection = ", ".join(f"{t}.txn_id" for t in t_aliases)
    sql = re.sub(r"SELECT \*", f"SELECT {projection}", sql, count=1)
    out = sorted(tuple(row) for row in c.execute(sql))
    conn.close()
    return out


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


def run_graphite_pipeline(query_path: str, hop_dir: str, sgx_app: str):
    """Graphite on a pre-built hop table: rewrite -> sgx_app. The hop table
    is built once by the caller and reused for every query."""
    with tempfile.TemporaryDirectory() as tmp:
        decomposed = os.path.join(tmp, "decomposed.sql")
        subprocess.run(["python3", REWRITER, query_path, decomposed],
                       check=True, capture_output=True, text=True)
        out = os.path.join(tmp, "out.csv")
        subprocess.run([sgx_app, decomposed, hop_dir, out],
                       check=True, capture_output=True, text=True)
        return parse_txn_tuples(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data_dir")
    ap.add_argument("onehop_bin")
    ap.add_argument("sgx_app")
    ap.add_argument("--queries", default=",".join(DEFAULT_QUERIES),
                    help=f"Comma-separated query names "
                         f"(default: {','.join(DEFAULT_QUERIES)})")
    ap.add_argument("--with-full-mwj", action="store_true",
                    help="Also validate sgx_app directly on the original "
                         "filtered query (slow; off by default).")
    args = ap.parse_args()
    queries = [q.strip() for q in args.queries.split(",") if q.strip()]

    print(f"Data: {args.data_dir}")
    print(f"Queries: {', '.join(queries)}")
    print()

    failures = []
    with tempfile.TemporaryDirectory() as hop_dir:
        print("Building hop table (once, reused for all queries)...", flush=True)
        subprocess.run([args.onehop_bin, args.data_dir,
                        os.path.join(hop_dir, "hop.csv")],
                       check=True, capture_output=True, text=True)

        hdr = f"{'query':<12} {'gold':>10} {'graphite':>10}"
        if args.with_full_mwj:
            hdr += f" {'mwj':>10}"
        print(hdr + "  result")
        print("-" * (50 + (11 if args.with_full_mwj else 0)))

        for q in queries:
            qpath = os.path.join(QUERY_DIR, f"{q}.sql")
            gold = sqlite_gold(args.data_dir, qpath)

            status_parts = []
            try:
                grf = run_graphite_pipeline(qpath, hop_dir, args.sgx_app)
                if grf != gold:
                    failures.append((q, "Graphite",
                                     f"{len(grf)} rows vs gold {len(gold)} -- mismatch"))
                    status_parts.append("GRAPHITE_MISMATCH")
            except subprocess.CalledProcessError as e:
                grf = None
                failures.append((q, "Graphite", f"crashed: {(e.stderr or '')[:200]}"))
                status_parts.append("GRAPHITE_ERR")

            mwj = None
            if args.with_full_mwj:
                try:
                    mwj = run_full_mwj(qpath, args.data_dir, args.sgx_app)
                    if mwj != gold:
                        failures.append((q, "FullMWJ",
                                         f"{len(mwj)} rows vs gold {len(gold)} -- mismatch"))
                        status_parts.append("MWJ_MISMATCH")
                except subprocess.CalledProcessError as e:
                    failures.append((q, "FullMWJ", f"crashed: {(e.stderr or '')[:200]}"))
                    status_parts.append("MWJ_ERR")

            status = " ".join(status_parts) or "OK"
            line = (f"{q:<12} {len(gold):>10} "
                    f"{('-' if grf is None else len(grf)):>10}")
            if args.with_full_mwj:
                line += f" {('-' if mwj is None else len(mwj)):>10}"
            print(f"{line}  {status}", flush=True)

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
