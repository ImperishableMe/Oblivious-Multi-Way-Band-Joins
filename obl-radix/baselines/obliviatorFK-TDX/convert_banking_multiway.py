#!/usr/bin/env python3
"""Convert banking CSVs into Obliviator multi-way driver inputs.

For a banking k-hop chain query (k accounts joined left-to-right via k-1 txns,
i.e. 2k+1 tables and 2k pairwise joins), this script emits:

  <output_dir>/account.txt   one-side elem_t file: key=account_id, data=full row
  <output_dir>/txn.txt       one-side elem_t file: key=acc_from,   data=full row
  <output_dir>/plan.txt      multi-way driver plan (see multiway_main.c)

Side-file format (consumed by multiway_main.c::load_side_file):
    <num_rows>
    <key> <data>
    ...

Banking schema (from docs/workloads.md):
    account.csv : account_id, balance, owner_id
    txn.csv     : acc_from,   acc_to,  amount, txn_time

All forward-direction chains in the workload doc key txn by acc_from on the
left side of each join; the next join's key (the acc_to of the same txn) is
carried in the .data payload and pulled by column index per the plan.

Filters are deferred to the final pairwise join's output per the threat model.

Usage:
    python3 convert_banking_multiway.py <hops> <account.csv> <txn.csv> <output_dir>
                                        [--filter "<col_in_final_concat> <op> <int>"] ...

    where <op> is one of: eq gt ge lt le.

Example (banking_2hop with default endpoint filters):
    python3 convert_banking_multiway.py 2 account.csv txn.csv plan_2hop \\
        --filter "2 eq 52" \\
        --filter "16 eq 45"
"""

import argparse
import csv
import os
import sys

ACCOUNT_COLS = ["account_id", "balance", "owner_id"]
TXN_COLS     = ["acc_from",   "acc_to",  "amount", "txn_time"]
ACCOUNT_KEY_IDX = 0   # account_id within ACCOUNT_COLS
TXN_KEY_IDX     = 0   # acc_from   within TXN_COLS
TXN_NEXT_IDX    = 1   # acc_to     within TXN_COLS  (the next-step key column)


def write_side_file(path, key_col_idx, csv_path, schema_cols):
    """Read csv_path, write Obliviator one-side file with key = csv col `key_col_idx`."""
    with open(csv_path, newline="") as f, open(path, "w") as out:
        reader = csv.reader(f)
        header = next(reader)
        # tolerate header order: re-map by name
        try:
            col_index = [header.index(c) for c in schema_cols]
        except ValueError as e:
            sys.exit(f"{csv_path}: missing column in header {header}: {e}")

        # Two-pass to write the row count first; cheap because we read once
        rows = []
        for row in reader:
            if not row:
                continue
            try:
                ints = [int(row[i]) for i in col_index]
            except ValueError:
                # skip sentinel / malformed rows silently — they'd break the int parse anyway
                continue
            # skip the sentinel row convention (-10000 in all cols)
            if all(v == -10000 for v in ints):
                continue
            rows.append(ints)

        out.write(f"{len(rows)}\n")
        for ints in rows:
            data = ",".join(str(v) for v in ints)
            out.write(f"{ints[key_col_idx]} {data}\n")
    return len(rows)


def build_plan(hops):
    """Generate the step list for a k-hop banking chain.

    Returns:
        steps: list of (side0, side1_path, next_key_col)
        cum_cols: list of cumulative column counts after each step's concat
        col_offset: dict mapping (alias, col_name) -> column index in final concat
    """
    n_acc = len(ACCOUNT_COLS)   # 3
    n_txn = len(TXN_COLS)       # 4

    steps = []
    cum = 0
    col_offset = {}

    # alias 1: a1
    for c, name in enumerate(ACCOUNT_COLS):
        col_offset[(f"a1", name)] = cum + c
    # step 0 inputs: a1 (cols 0..n_acc-1) + t1 (cols n_acc..n_acc+n_txn-1)
    cum += n_acc                           # 3
    for c, name in enumerate(TXN_COLS):
        col_offset[(f"t1", name)] = cum + c
    cum += n_txn                           # 7

    next_key_col_after_step0 = col_offset[("t1", "acc_to")]
    steps.append(("base:account.txt", "base:txn.txt", next_key_col_after_step0))

    # subsequent steps: alternating account / txn
    for hop in range(2, hops + 1):
        # step (2*hop-3): prev ⋈ a_{hop} on prev[t_{hop-1}.acc_to] = a_{hop}.account_id
        for c, name in enumerate(ACCOUNT_COLS):
            col_offset[(f"a{hop}", name)] = cum + c
        next_key_col = col_offset[(f"a{hop}", "account_id")]
        steps.append(("prev", "base:account.txt", next_key_col))
        cum += n_acc

        # step (2*hop-2): prev ⋈ t_{hop} on prev[a_{hop}.account_id] = t_{hop}.acc_from
        for c, name in enumerate(TXN_COLS):
            col_offset[(f"t{hop}", name)] = cum + c
        next_key_col = col_offset[(f"t{hop}", "acc_to")]
        steps.append(("prev", "base:txn.txt", next_key_col))
        cum += n_txn

    # final account a_{hops+1}
    for c, name in enumerate(ACCOUNT_COLS):
        col_offset[(f"a{hops+1}", name)] = cum + c
    steps.append(("prev", "base:account.txt", -1))     # last step
    cum += n_acc

    return steps, cum, col_offset


def write_plan(path, steps, total_cols, col_offset, filter_args):
    with open(path, "w") as f:
        f.write(f"# Banking {len(steps)//2}-hop chain plan ({len(steps)} pairwise joins).\n")
        f.write(f"# Final concat width: {total_cols} columns.\n\n")

        f.write(f"num_steps {len(steps)}\n")
        for i, (side0, side1, nkc) in enumerate(steps):
            f.write(f"step {i} side0={side0} side1={side1} next_key_col={nkc}\n")

        if filter_args:
            f.write("\n")
            for flt in filter_args:
                # expect "<col> <op> <int>"
                parts = flt.split()
                if len(parts) != 3 or parts[1] not in {"eq", "gt", "ge", "lt", "le"}:
                    sys.exit(f"bad --filter spec: {flt!r} (expected '<col> <op> <int>')")
                f.write(f"final_filter {parts[0]} {parts[1]} {parts[2]}\n")

        f.write("\n")
        # write schema in column-index order so the CSV header is in stable order
        ordered = sorted(col_offset.items(), key=lambda kv: kv[1])
        for (alias, name), idx in ordered:
            f.write(f"schema {idx} {alias}.{name}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hops", type=int, choices=range(1, 6),
                    help="chain depth (1..5); produces 2*hops pairwise steps")
    ap.add_argument("account_csv")
    ap.add_argument("txn_csv")
    ap.add_argument("output_dir")
    ap.add_argument("--filter", action="append", default=[],
                    help='final-stage filter: "<col_in_final_concat> <eq|gt|ge|lt|le> <int>". '
                         "Multiple --filter args are AND-conjoined.")
    args = ap.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    acct_path = os.path.join(args.output_dir, "account.txt")
    txn_path  = os.path.join(args.output_dir, "txn.txt")
    plan_path = os.path.join(args.output_dir, "plan.txt")

    n_acct = write_side_file(acct_path, ACCOUNT_KEY_IDX, args.account_csv, ACCOUNT_COLS)
    n_txn  = write_side_file(txn_path,  TXN_KEY_IDX,     args.txn_csv,     TXN_COLS)

    steps, total_cols, col_offset = build_plan(args.hops)
    write_plan(plan_path, steps, total_cols, col_offset, args.filter)

    print(f"Wrote {acct_path} ({n_acct} accounts)")
    print(f"Wrote {txn_path} ({n_txn} txns)")
    print(f"Wrote {plan_path} ({len(steps)} pairwise steps, {total_cols} cols in final concat)")
    print()
    print("Run with:")
    print(f"  ./multiway_main <num_threads> {plan_path} <output_csv>")
    print()
    print("Column index reference (for --filter):")
    for (alias, name), idx in sorted(col_offset.items(), key=lambda kv: kv[1]):
        print(f"  {idx:3d}  {alias}.{name}")


if __name__ == "__main__":
    main()
