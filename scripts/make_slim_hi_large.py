#!/usr/bin/env python3
"""
make_slim_hi_large.py — produce a column-trimmed ("slim") copy of an IBM AML
dataset for the E2 scaling experiment.

The chain "hop" queries (aml_2hop..aml_5hop) only ever reference
account.account_id, account.bank_id, txn.acc_from and txn.acc_to. The converted
HI-Large txn table carries four extra columns
(txn_time, currency, payment_format, is_laundering) that are dead weight: they
are copied into every per-alias deep copy of the materialized hop table and only
inflate the fixed-width in-memory row, pushing the >=2-hop join into OOM.

This tool streams the source txn.csv and writes a new txn.csv keeping only
  txn_id, acc_from, acc_to, amount
(amount is retained so the slim hop schema stays a clean 4-column edge table that
mirrors the banking workload; it is not a join key). account.csv is already
minimal (account_id, bank_id) so it is symlinked, never rewritten.

Memory use is O(1): the file is processed line-by-line, never loaded whole.
The original dataset directory is never mutated.

Usage:
    python3 scripts/make_slim_hi_large.py <src_dataset_dir> [<dst_dataset_dir>]

Defaults:
    dst_dataset_dir = input/plaintext/ibm_aml_hi_large_slim

Both paths may be absolute; when running from a git worktree the HI-Large data
lives only in the main checkout, so pass the main checkout's absolute paths.
"""

import csv
import os
import shutil
import sys

# Columns the chain/hop queries need, in the order the slim txn.csv will carry
# them. acc_from/acc_to are the join keys; txn_id is the edge identity; amount
# rides along so the slim edge schema matches the banking one-hop layout.
KEEP_COLUMNS = ["txn_id", "acc_from", "acc_to", "amount"]

DEFAULT_DST = os.path.join("input", "plaintext", "ibm_aml_hi_large_slim")

# Allow very wide CSV fields without tripping csv's default field-size limit.
csv.field_size_limit(1 << 24)


def slim_txn(src_txn_path, dst_txn_path):
    """Stream src txn.csv -> dst txn.csv keeping only KEEP_COLUMNS (by name)."""
    with open(src_txn_path, "r", newline="") as fin:
        reader = csv.reader(fin)
        try:
            header = next(reader)
        except StopIteration:
            raise RuntimeError(f"{src_txn_path} is empty (no header row)")

        name_to_idx = {name: i for i, name in enumerate(header)}
        missing = [c for c in KEEP_COLUMNS if c not in name_to_idx]
        if missing:
            raise RuntimeError(
                f"{src_txn_path} header {header} is missing required column(s): {missing}"
            )
        keep_idx = [name_to_idx[c] for c in KEEP_COLUMNS]

        data_rows = 0
        with open(dst_txn_path, "w", newline="") as fout:
            writer = csv.writer(fout)
            writer.writerow(KEEP_COLUMNS)
            for row in reader:
                # Skip blank trailing lines without disturbing real rows.
                if not row:
                    continue
                writer.writerow([row[i] for i in keep_idx])
                data_rows += 1

    return data_rows


def link_account(src_account_path, dst_account_path):
    """Symlink account.csv into the slim dir; fall back to a copy if needed."""
    if os.path.lexists(dst_account_path):
        os.remove(dst_account_path)
    try:
        os.symlink(os.path.abspath(src_account_path), dst_account_path)
        return "symlink"
    except OSError:
        shutil.copy2(src_account_path, dst_account_path)
        return "copy"


def main(argv):
    if len(argv) < 2 or len(argv) > 3:
        sys.stderr.write(
            "Usage: python3 scripts/make_slim_hi_large.py "
            "<src_dataset_dir> [<dst_dataset_dir>]\n"
        )
        return 2

    src_dir = argv[1]
    dst_dir = argv[2] if len(argv) == 3 else DEFAULT_DST

    src_txn = os.path.join(src_dir, "txn.csv")
    src_account = os.path.join(src_dir, "account.csv")
    for p in (src_txn, src_account):
        if not os.path.isfile(p):
            sys.stderr.write(f"Error: source file not found: {p}\n")
            return 1

    if os.path.abspath(src_dir) == os.path.abspath(dst_dir):
        sys.stderr.write("Error: dst_dataset_dir must differ from src_dataset_dir\n")
        return 1

    os.makedirs(dst_dir, exist_ok=True)
    dst_txn = os.path.join(dst_dir, "txn.csv")
    dst_account = os.path.join(dst_dir, "account.csv")

    print(f"Slimming {src_txn}")
    print(f"      -> {dst_txn}")
    print(f"   keeping columns: {', '.join(KEEP_COLUMNS)}")
    data_rows = slim_txn(src_txn, dst_txn)
    print(f"   wrote {data_rows} data rows")

    mode = link_account(src_account, dst_account)
    print(f"account.csv: {mode} {dst_account} -> {os.path.abspath(src_account)}")

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
