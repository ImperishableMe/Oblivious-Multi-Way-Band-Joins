#!/usr/bin/env python3
"""
Correctness gate for obliviator_khop_chained's query shapes (chain, fanin,
fanout) against SQLite gold on a small banking dataset.

For each (shape, K) the driver computes the UNFILTERED join (that is the
baseline's semantics), so gold is the unfiltered SQLite join with the same
structure, account joins included:

  chain  K: a1 -t1-> a2 -t2-> ... -tK-> a_{K+1}
  fanout K: hub -t1..tK-> a2..a_{K+1}   (all tk.acc_from = hub)
  fanin  K: a1..aK -t1..tK-> hub        (all tk.acc_to = hub)

Comparison key per row: the tuple of txn_id values in t1..tK order, compared
as sorted lists (multisets). Tuple-level comparison requires --threads 1:
at threads >= 2 the upstream NFK kernel has a documented pairing bug
(rowcount correct, tuple identities scrambled), so for threads >= 2 pass
--rowcount-only.

Usage:
  python3 tests/test_obliviator_shapes.py <data_dir> <khop_bin> <converter>
  python3 tests/test_obliviator_shapes.py input/plaintext/banking_1k \\
      obl-radix/baselines/obliviatorNFK-TDX/obliviator_khop_chained \\
      obl-radix/baselines/obliviatorFK-TDX/convert_banking_1hop.py \\
      [--threads 1] [--ks 2,3,4] [--acct-cols 3] [--txn-cols 4] [--rowcount-only]
"""

import argparse
import csv
import os
import sqlite3
import subprocess
import sys
import tempfile

SHAPES = ["chain", "fanout", "fanin"]


def load_sqlite(data_dir):
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
    return c


def gold_sql(shape, K):
    """Unfiltered join with account joins, projecting t1..tK txn_ids."""
    sel = ", ".join(f"t{k}.txn_id" for k in range(1, K + 1))
    if shape == "chain":
        tables = ["account AS a1"]
        conds = []
        for k in range(1, K + 1):
            tables += [f"txn AS t{k}", f"account AS a{k+1}"]
            conds += [f"t{k}.acc_from = a{k}.account_id",
                      f"a{k+1}.account_id = t{k}.acc_to"]
    elif shape == "fanout":
        tables = ["account AS hub"]
        conds = []
        for k in range(1, K + 1):
            tables += [f"txn AS t{k}", f"account AS a{k+1}"]
            conds += [f"t{k}.acc_from = hub.account_id",
                      f"a{k+1}.account_id = t{k}.acc_to"]
    elif shape == "fanin":
        tables = ["account AS hub"]
        conds = []
        for k in range(1, K + 1):
            tables += [f"txn AS t{k}", f"account AS a{k}"]
            conds += [f"t{k}.acc_to = hub.account_id",
                      f"a{k}.account_id = t{k}.acc_from"]
    else:
        raise ValueError(shape)
    return f"SELECT {sel} FROM {', '.join(tables)} WHERE {' AND '.join(conds)}"


def parse_driver_csv(path):
    with open(path) as f:
        r = csv.reader(f)
        header = next(r)
        positions = [i for i, n in enumerate(header) if n.endswith(".txn_id")]
        return sorted(tuple(int(row[i]) for i in positions) for row in r)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data_dir")
    ap.add_argument("khop_bin")
    ap.add_argument("converter")
    ap.add_argument("--threads", type=int, default=1,
                    help="Driver threads (default 1 = exact; >=2 implies "
                         "--rowcount-only unless overridden).")
    ap.add_argument("--ks", default="2,3,4",
                    help="Comma-separated K values (default: 2,3,4).")
    ap.add_argument("--acct-cols", type=int, default=3)
    ap.add_argument("--txn-cols", type=int, default=4)
    ap.add_argument("--rowcount-only", action="store_true",
                    help="Skip tuple-level comparison (use for threads >= 2).")
    args = ap.parse_args()
    ks = [int(k) for k in args.ks.split(",")]
    rowcount_only = args.rowcount_only or args.threads >= 2

    gold_cur = load_sqlite(args.data_dir)
    failures = []

    with tempfile.TemporaryDirectory() as tmp:
        src_txt = os.path.join(tmp, "src.txt")
        dst_txt = os.path.join(tmp, "dst.txt")
        subprocess.run(["python3", args.converter,
                        os.path.join(args.data_dir, "account.csv"),
                        os.path.join(args.data_dir, "txn.csv"),
                        src_txt, dst_txt],
                       check=True, capture_output=True, text=True)

        mode = "rowcount only" if rowcount_only else "rowcount + tuple multiset"
        print(f"Data: {args.data_dir}   threads={args.threads}   ({mode})")
        print(f"{'shape':<8} {'K':>2} {'gold':>10} {'driver':>10}  result")
        print("-" * 44)

        for shape in SHAPES:
            for K in ks:
                gold = sorted(tuple(row) for row in
                              gold_cur.execute(gold_sql(shape, K)))
                out_csv = os.path.join(tmp, f"{shape}_{K}.csv")
                cmd = [args.khop_bin, str(args.threads), str(K), src_txt,
                       out_csv, str(args.acct_cols), str(args.txn_cols), shape]
                try:
                    subprocess.run(cmd, check=True, capture_output=True, text=True)
                    got = parse_driver_csv(out_csv)
                    if len(got) != len(gold):
                        failures.append((shape, K,
                                         f"rowcount {len(got)} vs gold {len(gold)}"))
                        status = "ROWS_MISMATCH"
                    elif not rowcount_only and got != gold:
                        failures.append((shape, K, "tuple multiset mismatch"))
                        status = "TUPLE_MISMATCH"
                    else:
                        status = "OK"
                    got_n = len(got)
                except subprocess.CalledProcessError as e:
                    failures.append((shape, K, f"crashed: {(e.stderr or '')[:200]}"))
                    status, got_n = "ERR", "-"
                print(f"{shape:<8} {K:>2} {len(gold):>10} {got_n:>10}  {status}",
                      flush=True)

    print()
    if failures:
        print(f"FAILURES ({len(failures)}):")
        for shape, K, msg in failures:
            print(f"  {shape} K={K}: {msg}")
        sys.exit(1)
    print("All obliviator shape checks passed.")
    sys.exit(0)


if __name__ == "__main__":
    main()
