#!/usr/bin/env python3
"""
Convert IBM AML-Data transaction CSV to NebulaDB format (W4).

IBM AML-Data (Altman et al., NeurIPS 2023) ships one transaction file per
variant (HI-Small_Trans.csv, HI-Medium_Trans.csv, HI-Large_Trans.csv) with
the columns:
    Timestamp, From Bank, Account, To Bank, Account.1,
    Amount Received, Receiving Currency, Amount Paid, Payment Currency,
    Payment Format, Is Laundering

Our pipeline needs two int32 CSVs:
  account.csv: account_id, bank_id
  txn.csv:     txn_id, acc_from, acc_to, amount, txn_time,
               currency, payment_format, is_laundering

Conversions performed:
  - Account hex strings are remapped to contiguous int [1, N].
  - Timestamps are converted to epoch days (Unix seconds // 86400).
  - Amounts are stored as integer cents (round(Amount Paid * 100)).
  - Categorical columns (Payment Currency, Payment Format) are mapped to
    integer codes based on first-seen order.
  - All values are validated to fit within [-1_073_741_820, 1_073_741_820].

Operates in a single streaming pass — safe for HI-Medium (32M txns) and
HI-Large (180M txns).

Usage:
    python3 scripts/convert_ibm_aml_to_nebuladb.py \\
        <path/to/HI-Small_Trans.csv> <output_dir>
"""

import argparse
import csv
import random
import sys
from collections import Counter, OrderedDict
from datetime import datetime
from pathlib import Path

# System bounds from enclave_types.h
JOIN_ATTR_MIN = -1_073_741_820
JOIN_ATTR_MAX = 1_073_741_820

# Expected IBM AML input columns
EXPECTED_COLS = [
    "Timestamp", "From Bank", "Account", "To Bank", "Account.1",
    "Amount Received", "Receiving Currency", "Amount Paid",
    "Payment Currency", "Payment Format", "Is Laundering",
]

RESERVOIR_SIZE = 100_000  # samples kept for amount percentile estimation
PROGRESS_EVERY = 1_000_000


def parse_timestamp_to_epoch_days(ts: str) -> int:
    """`2022/09/01 00:20` → Unix epoch days."""
    # Try full minute-precision first; fall back to date-only.
    try:
        dt = datetime.strptime(ts, "%Y/%m/%d %H:%M")
    except ValueError:
        dt = datetime.strptime(ts, "%Y/%m/%d")
    return int(dt.timestamp()) // 86400


def check_bounds(val: int, col_name: str) -> None:
    if not (JOIN_ATTR_MIN <= val <= JOIN_ATTR_MAX):
        raise ValueError(f"{col_name} value {val} outside int32 range "
                         f"[{JOIN_ATTR_MIN}, {JOIN_ATTR_MAX}]")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input_csv", type=Path,
                        help="IBM AML transaction CSV (e.g. HI-Small_Trans.csv)")
    parser.add_argument("output_dir", type=Path,
                        help="Output directory (will be created)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Seed for reservoir sampling (default: 42)")
    args = parser.parse_args()

    random.seed(args.seed)

    if not args.input_csv.is_file():
        print(f"ERROR: input CSV not found: {args.input_csv}", file=sys.stderr)
        return 1

    args.output_dir.mkdir(parents=True, exist_ok=True)
    account_csv_path = args.output_dir / "account.csv"
    txn_csv_path = args.output_dir / "txn.csv"

    print("=" * 70)
    print("IBM AML-Data → NebulaDB W4 converter")
    print("=" * 70)
    print(f"Input : {args.input_csv}")
    print(f"Output: {args.output_dir}/")
    print()

    # State built during the streaming pass.
    account_map: dict[tuple[int, str], int] = {}  # (bank, hex) → account_id
    account_bank: list[int] = []                  # account_id → bank_id (index = id - 1)
    currency_map: "OrderedDict[str, int]" = OrderedDict()
    format_map: "OrderedDict[str, int]" = OrderedDict()
    bank_account_counter: Counter = Counter()    # bank_id → #distinct accounts

    reservoir: list[int] = []
    reservoir_seen = 0

    def get_account_id(bank: int, acct_hex: str) -> int:
        key = (bank, acct_hex)
        existing = account_map.get(key)
        if existing is not None:
            return existing
        new_id = len(account_map) + 1
        account_map[key] = new_id
        account_bank.append(bank)
        bank_account_counter[bank] += 1
        return new_id

    def encode(mapping: "OrderedDict[str, int]", value: str) -> int:
        code = mapping.get(value)
        if code is None:
            code = len(mapping) + 1  # 1-indexed so 0 stays free as "unknown"
            mapping[value] = code
        return code

    laundering_count = 0
    min_epoch_day = None
    max_epoch_day = None
    min_amount_cents: int | None = None
    max_amount_cents: int | None = None

    with open(args.input_csv, "r", newline="") as fin, \
         open(txn_csv_path, "w", newline="") as ftxn:

        reader = csv.reader(fin)
        header = next(reader)
        if header != EXPECTED_COLS:
            print("WARNING: unexpected column layout.")
            print(f"  expected: {EXPECTED_COLS}")
            print(f"  found   : {header}")

        txn_writer = csv.writer(ftxn, lineterminator="\n")
        txn_writer.writerow([
            "txn_id", "acc_from", "acc_to", "amount", "txn_time",
            "currency", "payment_format", "is_laundering",
        ])

        txn_id = 0
        for row in reader:
            if len(row) != len(EXPECTED_COLS):
                raise ValueError(f"Row {txn_id + 2} has {len(row)} fields, "
                                 f"expected {len(EXPECTED_COLS)}: {row!r}")

            (ts_str, from_bank_s, from_acct, to_bank_s, to_acct,
             _amount_recv, _recv_ccy, amount_paid_s, pay_ccy_s,
             pay_fmt_s, is_launder_s) = row

            from_bank = int(from_bank_s)
            to_bank = int(to_bank_s)
            check_bounds(from_bank, "From Bank")
            check_bounds(to_bank, "To Bank")

            acc_from = get_account_id(from_bank, from_acct)
            acc_to = get_account_id(to_bank, to_acct)

            txn_time = parse_timestamp_to_epoch_days(ts_str)
            check_bounds(txn_time, "txn_time")
            if min_epoch_day is None or txn_time < min_epoch_day:
                min_epoch_day = txn_time
            if max_epoch_day is None or txn_time > max_epoch_day:
                max_epoch_day = txn_time

            amount_cents = int(round(float(amount_paid_s) * 100))
            check_bounds(amount_cents, "amount")
            if min_amount_cents is None or amount_cents < min_amount_cents:
                min_amount_cents = amount_cents
            if max_amount_cents is None or amount_cents > max_amount_cents:
                max_amount_cents = amount_cents

            currency = encode(currency_map, pay_ccy_s)
            pay_fmt = encode(format_map, pay_fmt_s)
            is_launder = 1 if is_launder_s.strip() in ("1", "True", "true") else 0
            laundering_count += is_launder

            txn_id += 1
            txn_writer.writerow([
                txn_id, acc_from, acc_to, amount_cents, txn_time,
                currency, pay_fmt, is_launder,
            ])

            # Reservoir sample the amount for percentile estimation.
            reservoir_seen += 1
            if len(reservoir) < RESERVOIR_SIZE:
                reservoir.append(amount_cents)
            else:
                j = random.randint(0, reservoir_seen - 1)
                if j < RESERVOIR_SIZE:
                    reservoir[j] = amount_cents

            if txn_id % PROGRESS_EVERY == 0:
                print(f"  ... {txn_id:,} transactions processed")

    # Write account.csv from the accumulated map, ordered by account_id.
    print(f"\nWriting account.csv ({len(account_map):,} accounts)...")
    with open(account_csv_path, "w", newline="") as facc:
        acc_writer = csv.writer(facc, lineterminator="\n")
        acc_writer.writerow(["account_id", "bank_id"])
        for idx, bank_id in enumerate(account_bank, start=1):
            acc_writer.writerow([idx, bank_id])

    # ---- Stats ----
    print("\n" + "=" * 70)
    print("Conversion summary")
    print("=" * 70)
    print(f"  Transactions: {txn_id:,}")
    print(f"  Accounts:     {len(account_map):,}")
    print(f"  Distinct banks: {len(bank_account_counter):,}")
    print(f"  Labeled laundering txns: {laundering_count:,} "
          f"(rate 1/{txn_id // max(laundering_count, 1)})")
    print(f"  Currency codes: {len(currency_map)} "
          f"(first few: {list(currency_map.items())[:5]})")
    print(f"  Payment format codes: {len(format_map)} "
          f"(first few: {list(format_map.items())[:5]})")
    print(f"  txn_time range: [{min_epoch_day}, {max_epoch_day}] "
          f"(epoch days, span {max_epoch_day - min_epoch_day + 1 if min_epoch_day is not None else 0} days)")
    print(f"  amount_cents range: [{min_amount_cents}, {max_amount_cents}]")

    # Top banks by account count — useful for picking the a1.bank_id anchor.
    print("\n  Top 20 banks by account count (pick one as bank_id anchor):")
    print(f"    {'rank':>4}  {'bank_id':>10}  {'# accts':>10}")
    for rank, (bank, count) in enumerate(
            bank_account_counter.most_common(20), start=1):
        print(f"    {rank:>4}  {bank:>10}  {count:>10,}")

    # Amount percentiles — pick thresholds for selectivity variants.
    if reservoir:
        reservoir.sort()
        def pct(p: float) -> int:
            idx = min(int(p * len(reservoir)), len(reservoir) - 1)
            return reservoir[idx]
        print("\n  Amount (cents) percentiles — pick thresholds for aml_sel_*:")
        print(f"    ~80% pass (p20):  amount > {pct(0.20):>12,}   (aml_sel_low)")
        print(f"    ~60% pass (p40):  amount > {pct(0.40):>12,}   (aml_sel_med)")
        print(f"    ~40% pass (p60):  amount > {pct(0.60):>12,}   (aml_sel_high)")
        print(f"    ~20% pass (p80):  amount > {pct(0.80):>12,}   (aml_sel_vhigh)")

    # Suggested anchor: a bank with a few thousand accounts (not the largest).
    candidates = [(b, c) for b, c in bank_account_counter.items()
                  if 500 <= c <= 10_000]
    candidates.sort(key=lambda x: x[1])
    if candidates:
        suggested = candidates[len(candidates) // 2]
        print(f"\n  Suggested bank_id anchor: {suggested[0]} "
              f"({suggested[1]} accounts) — sits in the 500–10K-accounts band.")
    else:
        # Fall back to whatever exists.
        median_bank, median_count = bank_account_counter.most_common()[
            len(bank_account_counter) // 2]
        print(f"\n  Suggested bank_id anchor: {median_bank} "
              f"({median_count} accounts) — median-size bank.")

    print(f"\nDone. Outputs:")
    print(f"  {account_csv_path}")
    print(f"  {txn_csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
