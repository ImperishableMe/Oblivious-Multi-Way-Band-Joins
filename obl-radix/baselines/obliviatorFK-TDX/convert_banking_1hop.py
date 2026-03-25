"""Convert banking CSV files to Obliviator FK input format for 1-hop query.

Produces two input files:
  - src file: joins txn.acc_from = account.account_id
  - dst file: joins txn.acc_to   = account.account_id

Usage:
  python3 convert_banking_1hop.py <account.csv> <txn.csv> <out_src.txt> <out_dst.txt>
"""

import sys

def main():
    if len(sys.argv) != 5:
        print(f"usage: {sys.argv[0]} <account.csv> <txn.csv> <out_src.txt> <out_dst.txt>")
        sys.exit(1)

    account_file, txn_file, out_src, out_dst = sys.argv[1:]

    # Read accounts (skip header)
    accounts = []
    with open(account_file) as f:
        next(f)  # skip header
        for line in f:
            parts = line.strip().split(',')
            if len(parts) < 3:
                continue
            account_id, balance, owner_id = parts[0], parts[1], parts[2]
            accounts.append((account_id, f"{balance},{owner_id}"))

    # Read transactions (skip header)
    txns = []
    with open(txn_file) as f:
        next(f)  # skip header
        for line in f:
            parts = line.strip().split(',')
            if len(parts) < 4:
                continue
            acc_from, acc_to, amount, txn_time = parts[0], parts[1], parts[2], parts[3]
            txns.append((acc_from, acc_to, amount, txn_time))

    num_accounts = len(accounts)
    num_txns = len(txns)
    print(f"Accounts: {num_accounts}, Transactions: {num_txns}")

    # Source join: key=acc_from for txns, key=account_id for accounts
    with open(out_src, 'w') as f:
        f.write(f"{num_accounts} {num_txns}\n\n")
        for aid, data in accounts:
            f.write(f"{aid} {data}\n")
        f.write("\n")
        for acc_from, acc_to, amount, txn_time in txns:
            f.write(f"{acc_from} {acc_to},{amount},{txn_time}\n")

    # Destination join: key=acc_to for txns, key=account_id for accounts
    with open(out_dst, 'w') as f:
        f.write(f"{num_accounts} {num_txns}\n\n")
        for aid, data in accounts:
            f.write(f"{aid} {data}\n")
        f.write("\n")
        for acc_from, acc_to, amount, txn_time in txns:
            f.write(f"{acc_to} {acc_from},{amount},{txn_time}\n")

    print(f"Written: {out_src} (source join), {out_dst} (destination join)")

if __name__ == "__main__":
    main()
