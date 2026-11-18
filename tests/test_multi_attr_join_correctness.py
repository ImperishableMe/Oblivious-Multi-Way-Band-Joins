#!/usr/bin/env python3
"""
Correctness test for multi-attribute join keys in the oblivious multi-way join
(sgx_app).

Two tables may be joined on several conditions at once: any number of
equalities plus at most one range, for example

    s.a = r.a AND s.b = r.b AND s.t >= r.t AND s.t <= r.t + 5

The engine folds these into one composite key (equality columns first, the
range column last) and evaluates them inside the join, so the only size it
reveals is the size of the final, already-filtered result.

Every supported case is checked row-for-row against SQLite on the same data.
Queries the engine must refuse (two ranges on different column pairs, a
predicate that closes a cycle in the join graph, '<>' between tables, ...)
are checked for a clean non-zero exit instead of a silently wrong answer.

The cases cover every join-tree shape: two tables, chains, a root with several
children, and nodes that have both a parent and two or more children (which
the align-concat phase handles with rank-based alignment keys).

Usage:
    python3 tests/test_multi_attr_join_correctness.py <sgx_app_binary> [banking_data_dir]

Example:
    python3 tests/test_multi_attr_join_correctness.py ./sgx_app input/plaintext/banking_1k

The banking data directory (default: input/plaintext/banking_1k) supplies a
realistic hop table (one row per transaction with both endpoint accounts) for
the multi-hop cases. If it is missing those cases are skipped and reported.
"""

import csv
import os
import random
import sqlite3
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BANKING_DIR = os.path.join(REPO_ROOT, "input", "plaintext", "banking_1k")

# ---------------------------------------------------------------------------
# Synthetic tables. Domains are deliberately tiny so that keys collide, groups
# sit next to each other with identical range values, and range boundaries are
# hit exactly. Column `b` and `t` take negative values.
# ---------------------------------------------------------------------------
SYNTHETIC_SCHEMAS = {
    "rel_r": ["rid", "a", "b", "t", "v"],
    "rel_s": ["sid", "a", "b", "t", "w"],
    "rel_u": ["uid", "a", "b", "t", "x"],
    # No id column and only 12 possible rows, so most rows occur several times.
    "rel_d": ["a", "b", "t"],
}
ROWS_PER_TABLE = 40


def make_synthetic_rows(seed: int, t_base: int) -> dict:
    rng = random.Random(seed)
    tables = {}
    for name in SYNTHETIC_SCHEMAS:
        rows = []
        for i in range(ROWS_PER_TABLE):
            if name == "rel_d":
                rows.append([
                    rng.randint(0, 2),               # a
                    rng.randint(0, 1),               # b
                    t_base + rng.randint(0, 1),      # t
                ])
                continue
            rows.append([
                i + 1,                           # id
                rng.randint(0, 4),               # a
                rng.randint(-1, 1),              # b
                t_base + rng.randint(-6, 6),     # t
                rng.randint(0, 99),              # payload
            ])
        tables[name] = rows
    return tables


# ---------------------------------------------------------------------------
# Test cases: (name, FROM items [(table, alias)], WHERE conjuncts)
# The first table in FROM is the root of the engine's join tree.
# ---------------------------------------------------------------------------
R, S, U = ("rel_r", "r"), ("rel_s", "s"), ("rel_u", "u")
R2, S2, U2 = ("rel_r", "r2"), ("rel_s", "s2"), ("rel_u", "u2")
D = ("rel_d", "d")

SYNTHETIC_CASES = [
    # --- two tables -------------------------------------------------------
    ("eq1_single_key", [R, S], ["r.a = s.a"]),
    ("band_only_single_key", [R, S], ["s.t >= r.t - 1", "s.t <= r.t + 1"]),
    ("eq2", [R, S], ["r.a = s.a", "r.b = s.b"]),
    ("eq3", [R, S], ["r.a = s.a", "r.b = s.b", "r.t = s.t"]),
    ("eq1_band_closed", [R, S], ["r.a = s.a", "s.t >= r.t - 2", "s.t <= r.t + 3"]),
    ("eq1_band_open", [R, S], ["r.a = s.a", "s.t > r.t - 2", "s.t < r.t + 3"]),
    ("eq1_band_half_open", [R, S], ["r.a = s.a", "s.t >= r.t", "s.t < r.t + 3"]),
    ("eq1_le", [R, S], ["r.a = s.a", "s.t <= r.t + 1"]),
    ("eq1_lt", [R, S], ["r.a = s.a", "s.t < r.t"]),
    ("eq1_ge", [R, S], ["r.a = s.a", "s.t >= r.t - 1"]),
    ("eq1_gt", [R, S], ["r.a = s.a", "s.t > r.t"]),
    ("eq2_band", [R, S], ["r.a = s.a", "r.b = s.b", "s.t >= r.t - 2", "s.t <= r.t + 2"]),
    ("band_written_first", [R, S], ["s.t <= r.t + 3", "s.t >= r.t - 2", "s.a = r.a"]),
    ("bounds_opposite_orientation", [R, S], ["r.a = s.a", "r.t <= s.t + 2", "s.t <= r.t + 3"]),
    ("eq_with_offset_and_band", [R, S], ["s.b = r.b + 1", "s.t >= r.t", "s.t <= r.t + 4"]),
    ("eq_and_eq_with_offset", [R, S], ["s.a = r.a", "s.b = r.b - 1"]),
    ("band_on_other_columns", [R, S], ["r.a = s.a", "s.w >= r.v - 10", "s.w <= r.v + 10"]),
    ("composite_with_filters", [R, S],
     ["r.a = s.a", "s.t >= r.t", "s.t <= r.t + 3", "r.v > 30", "s.w <= 70"]),
    ("self_join_composite", [("rel_r", "r1"), ("rel_r", "r2")], ["r1.a = r2.a", "r2.t > r1.t"]),
    # --- three tables, chain r - s - u -------------------------------------
    ("chain_window_on_both_edges", [R, S, U],
     ["r.a = s.a", "s.t >= r.t", "s.t <= r.t + 2",
      "s.b = u.b", "u.t >= s.t", "u.t <= s.t + 2"]),
    ("chain_eq2_then_eq_range", [R, S, U],
     ["r.a = s.a", "r.b = s.b", "s.a = u.a", "u.t > s.t"]),
    ("chain_composite_plain_filter", [R, S, U],
     ["r.a = s.a", "s.t >= r.t - 1", "s.t <= r.t + 1", "s.b = u.b", "u.x < 50"]),
    # --- three tables, star: r is the parent of both s and u ---------------
    ("star_same_key_column", [R, S, U], ["r.a = s.a", "r.a = u.a", "s.w < 40", "u.x < 40"]),
    ("star_different_key_columns", [R, S, U], ["r.a = s.a", "r.b = u.b", "s.w < 40", "u.x < 40"]),
    ("star_composite_different_keys", [R, S, U],
     ["r.a = s.a", "s.t >= r.t", "s.t <= r.t + 2",
      "r.b = u.b", "u.t >= r.t - 1", "u.t <= r.t + 1"]),
    # --- conditions that close a cycle but add nothing -----------------------
    # Three tables sharing one attribute: the third equality (or a range that
    # the equalities already satisfy) follows from the other two and must be
    # accepted, not mistaken for a real cycle.
    ("shared_attribute_redundant_equality", [R, S, U],
     ["r.a = s.a", "s.a = u.a", "r.a = u.a", "s.w < 30", "u.x < 30"]),
    ("shared_attribute_redundant_range", [R, S, U],
     ["r.a = s.a", "s.a = u.a", "u.a >= r.a", "s.w < 30", "u.x < 30"]),
    ("redundant_equality_through_offsets", [R, S, U],
     ["s.b = r.b + 1", "u.b = s.b - 1", "u.b = r.b", "s.w < 30", "u.x < 30"]),
    # --- a node with a parent AND two or more children ----------------------
    # s is joined to its parent r and to its children u and r2 (r2, s2, u2 are
    # further aliases of the same tables). The filters keep the results small.
    ("inner_two_children_same_column", [R, S, U, R2],
     ["r.a = s.a", "s.a = u.a", "s.a = r2.a", "u.x < 50", "r2.v < 50"]),
    ("inner_two_children_different_columns", [R, S, U, R2],
     ["r.a = s.a", "s.b = u.b", "s.a = r2.a", "u.x < 30", "r2.v < 30"]),
    ("inner_two_children_composite", [R, S, U, R2],
     ["r.a = s.a", "s.t >= r.t", "s.t <= r.t + 1",
      "s.b = u.b", "u.t = s.t",
      "s.a = r2.a", "r2.b = s.b", "r2.t > s.t"]),
    ("inner_three_children", [R, S, U, R2, S2],
     ["r.a = s.a", "s.a = u.a", "s.b = r2.b", "s.a = s2.a",
      "u.x < 20", "r2.v < 20", "s2.w < 20"]),
    ("two_levels_of_branching", [R, S, U, R2, S2, U2],
     ["r.a = s.a", "s.a = u.a", "s.b = r2.b", "u.b = s2.b", "u.a = u2.a",
      "r.v < 50", "u.x < 50", "r2.v < 20", "s2.w < 15", "u2.x < 15"]),
    # --- tables with repeated rows -------------------------------------------
    ("repeated_rows_leaf", [R, D], ["r.a = d.a"]),
    ("chain_through_repeated_rows", [R, D, U], ["r.a = d.a", "d.b = u.b", "u.x < 30"]),
    ("chain_repeated_rows_then_two_more", [R, D, S, U],
     ["r.a = d.a", "d.b = s.b", "s.a = u.a", "s.w < 30", "u.x < 30"]),
    ("root_with_repeated_rows", [D, U, R2],
     ["d.b = u.b", "d.a = r2.a", "u.x < 20", "r2.v < 30"]),
    ("inner_repeated_rows_two_children", [R, D, U, R2],
     ["r.a = d.a", "d.b = u.b", "d.a = r2.a", "u.x < 20", "r2.v < 30"]),
]

# Queries that must be refused with a non-zero exit code.
REJECT_CASES = [
    ("reject_two_ranges", [R, S], ["r.a = s.a", "s.t <= r.t + 1", "s.w >= r.v"]),
    ("reject_cycle", [R, S, U], ["r.a = s.a", "s.a = u.a", "u.t >= r.t"]),
    ("reject_too_many_equalities", [R, S],
     ["r.a = s.a", "r.b = s.b", "r.t = s.t", "r.v = s.w", "r.rid = s.sid"]),
    ("reject_not_equal", [R, S], ["r.a = s.a", "r.b <> s.b"]),
    ("reject_contradictory_range", [R, S], ["r.a = s.a", "s.t >= r.t + 5", "s.t <= r.t + 1"]),
    ("reject_same_table_comparison", [R, S], ["r.a = s.a", "r.t < r.v"]),
    # closes a cycle and contradicts what the other equalities force (u.a = r.a)
    ("reject_cycle_contradicting_equalities", [R, S, U],
     ["r.a = s.a", "s.a = u.a", "u.a > r.a"]),
    # the equality part is redundant, the range part is a real new condition
    ("reject_cycle_partly_redundant", [R, S, U],
     ["r.a = s.a", "s.a = u.a", "r.a = u.a", "u.t >= r.t"]),
]

# Hop-table cases (banking data): the shape Graphite's residual query has.
H1, H2, H3 = ("hop", "h1"), ("hop", "h2"), ("hop", "h3")
NODE_12 = "h1.account_dest_account_id = h2.account_src_account_id"
NODE_23 = "h2.account_dest_account_id = h3.account_src_account_id"
WINDOW = 100000
HOP_CASES = [
    ("hop_2hop_node_only", [H1, H2], [NODE_12]),
    ("hop_2hop_time_window", [H1, H2],
     [NODE_12, "h2.txn_time >= h1.txn_time", f"h2.txn_time <= h1.txn_time + {WINDOW}"]),
    ("hop_2hop_within_window_one_sided", [H1, H2],
     [NODE_12, f"h2.txn_time <= h1.txn_time + {WINDOW}"]),
    ("hop_2hop_strictly_after", [H1, H2], [NODE_12, "h2.txn_time > h1.txn_time"]),
    ("hop_2hop_same_owner_and_after", [H1, H2],
     [NODE_12, "h1.account_src_owner_id = h2.account_dest_owner_id",
      "h2.txn_time > h1.txn_time"]),
    ("hop_3hop_window_on_both_pairs", [H1, H2, H3],
     [NODE_12, "h2.txn_time >= h1.txn_time", f"h2.txn_time <= h1.txn_time + {WINDOW}",
      NODE_23, "h3.txn_time >= h2.txn_time", f"h3.txn_time <= h2.txn_time + {WINDOW}"]),
    ("hop_3hop_window_plain_edge_filter", [H1, H2, H3],
     [NODE_12, "h2.txn_time >= h1.txn_time", f"h2.txn_time <= h1.txn_time + {WINDOW}",
      NODE_23, "h3.amount > 50000"]),
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def write_csv(path: str, header: list, rows: list) -> None:
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(rows)


def read_csv_ints(path: str) -> tuple:
    with open(path, "r") as f:
        reader = csv.reader(f)
        header = next(reader)
        rows = [[int(v) for v in row] for row in reader]
    return header, rows


def load_into_sqlite(conn: sqlite3.Connection, name: str, header: list, rows: list) -> None:
    cols = ", ".join(f'"{c}" INTEGER' for c in header)
    conn.execute(f'CREATE TABLE "{name}" ({cols})')
    placeholders = ", ".join("?" for _ in header)
    conn.executemany(f'INSERT INTO "{name}" VALUES ({placeholders})', rows)


def build_sql(from_items: list, conds: list) -> str:
    from_clause = ", ".join(f"{table} AS {alias}" for table, alias in from_items)
    return f"SELECT * FROM {from_clause}\nWHERE " + "\n  AND ".join(conds) + ";\n"


def expected_rows(conn: sqlite3.Connection, schemas: dict, from_items: list, conds: list) -> list:
    """SQLite result with columns ordered alias-by-alias in FROM order."""
    select_cols = ", ".join(
        f'{alias}."{col}"' for table, alias in from_items for col in schemas[table]
    )
    from_clause = ", ".join(f'"{table}" AS {alias}' for table, alias in from_items)
    sql = f"SELECT {select_cols} FROM {from_clause} WHERE " + " AND ".join(conds)
    return sorted(conn.execute(sql).fetchall())


def run_engine(sgx_app: str, data_dir: str, work_dir: str, name: str, sql: str) -> tuple:
    """Returns (returncode, output_csv_path, combined stdout+stderr)."""
    file_stem = name.replace("/", "_")
    sql_path = os.path.join(work_dir, f"{file_stem}.sql")
    out_path = os.path.join(work_dir, f"{file_stem}.out.csv")
    with open(sql_path, "w") as f:
        f.write(sql)
    if os.path.exists(out_path):
        os.unlink(out_path)
    result = subprocess.run([sgx_app, sql_path, data_dir, out_path], capture_output=True, text=True)
    return result.returncode, out_path, result.stdout + result.stderr


def engine_rows(out_path: str, schemas: dict, from_items: list) -> list:
    """Engine output re-ordered to the same canonical column order as expected_rows."""
    header, rows = read_csv_ints(out_path)
    wanted = [f"{alias}.{col}" for table, alias in from_items for col in schemas[table]]
    missing = [c for c in wanted if c not in header]
    if missing or len(header) != len(wanted):
        raise AssertionError(
            f"output schema mismatch: missing={missing}, got {len(header)} columns, "
            f"expected {len(wanted)}"
        )
    index = [header.index(c) for c in wanted]
    return sorted(tuple(row[i] for i in index) for row in rows)


def check_case(sgx_app, data_dir, work_dir, conn, schemas, label, from_items, conds) -> bool:
    sql = build_sql(from_items, conds)
    want = expected_rows(conn, schemas, from_items, conds)
    rc, out_path, log = run_engine(sgx_app, data_dir, work_dir, label, sql)
    if rc != 0 or not os.path.exists(out_path):
        print(f"FAIL [{label}]: sgx_app exited with code {rc}")
        print("      " + "\n      ".join(log.strip().splitlines()[-4:]))
        return False
    try:
        got = engine_rows(out_path, schemas, from_items)
    except AssertionError as exc:
        print(f"FAIL [{label}]: {exc}")
        return False
    if got != want:
        print(f"FAIL [{label}]: engine returned {len(got)} rows, SQLite {len(want)}"
              + ("" if len(got) != len(want) else " (same count, different rows)"))
        return False
    print(f"PASS [{label}]: {len(got)} rows match SQLite exactly")
    return True


def check_reject(sgx_app, data_dir, work_dir, label, from_items, conds) -> bool:
    sql = build_sql(from_items, conds)
    rc, out_path, log = run_engine(sgx_app, data_dir, work_dir, label, sql)
    if rc == 0:
        produced = "and wrote a result" if os.path.exists(out_path) else "without a result"
        print(f"FAIL [{label}]: query was accepted (exit 0 {produced}); it must be refused")
        return False
    reason = [ln for ln in log.strip().splitlines() if ln.startswith("Error:")]
    print(f"PASS [{label}]: refused ({reason[-1] if reason else 'exit code ' + str(rc)})")
    return True


def build_hop_table(banking_dir: str) -> tuple:
    """One row per transaction with both endpoint accounts (the one-hop result shape)."""
    acc_header, acc_rows = read_csv_ints(os.path.join(banking_dir, "account.csv"))
    txn_header, txn_rows = read_csv_ints(os.path.join(banking_dir, "txn.csv"))
    for needed, header, fname in ((["account_id"], acc_header, "account.csv"),
                                  (["acc_from", "acc_to", "txn_time", "amount"], txn_header, "txn.csv")):
        absent = [c for c in needed if c not in header]
        if absent:
            raise RuntimeError(f"{fname} is missing expected columns: {absent}")
    conn = sqlite3.connect(":memory:")
    load_into_sqlite(conn, "account", acc_header, acc_rows)
    load_into_sqlite(conn, "txn", txn_header, txn_rows)
    hop_header = (list(txn_header)
                  + [f"account_src_{c}" for c in acc_header]
                  + [f"account_dest_{c}" for c in acc_header])
    select = (", ".join(f't."{c}"' for c in txn_header) + ", "
              + ", ".join(f's."{c}"' for c in acc_header) + ", "
              + ", ".join(f'd."{c}"' for c in acc_header))
    hop_rows = conn.execute(
        f"SELECT {select} FROM txn AS t, account AS s, account AS d "
        "WHERE s.account_id = t.acc_from AND d.account_id = t.acc_to"
    ).fetchall()
    conn.close()
    return hop_header, [list(r) for r in hop_rows]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    if len(sys.argv) not in (2, 3):
        print(f"Usage: {sys.argv[0]} <sgx_app_binary> [banking_data_dir]")
        print(f"Example: {sys.argv[0]} ./sgx_app input/plaintext/banking_1k")
        sys.exit(1)

    sgx_app = sys.argv[1]
    banking_dir = sys.argv[2] if len(sys.argv) == 3 else DEFAULT_BANKING_DIR
    if not os.path.isfile(sgx_app):
        print(f"ERROR: sgx_app binary not found: {sgx_app}")
        sys.exit(1)

    passed = failed = skipped = 0

    with tempfile.TemporaryDirectory(prefix="multi_attr_join_") as tmp:
        # (seed, t_base): the last run shifts the range column to large values to
        # check the composite comparison far away from zero.
        runs = [(1, 0), (2, 0), (3, 500000000)]
        for seed, t_base in runs:
            print(f"\n=== Synthetic tables, seed {seed}, t offset {t_base} ===")
            data_dir = os.path.join(tmp, f"synthetic_{seed}")
            work_dir = os.path.join(tmp, f"work_{seed}")
            os.makedirs(data_dir)
            os.makedirs(work_dir)
            tables = make_synthetic_rows(seed, t_base)
            conn = sqlite3.connect(":memory:")
            for name, rows in tables.items():
                write_csv(os.path.join(data_dir, f"{name}.csv"), SYNTHETIC_SCHEMAS[name], rows)
                load_into_sqlite(conn, name, SYNTHETIC_SCHEMAS[name], rows)
            for name, from_items, conds in SYNTHETIC_CASES:
                ok = check_case(sgx_app, data_dir, work_dir, conn, SYNTHETIC_SCHEMAS,
                                f"{name}/seed{seed}", from_items, conds)
                passed, failed = passed + ok, failed + (not ok)
            if seed == runs[0][0]:
                print("\n=== Queries that must be refused ===")
                for name, from_items, conds in REJECT_CASES:
                    ok = check_reject(sgx_app, data_dir, work_dir, name, from_items, conds)
                    passed, failed = passed + ok, failed + (not ok)
            conn.close()

        print(f"\n=== Hop table from {banking_dir} ===")
        if not (os.path.isfile(os.path.join(banking_dir, "account.csv"))
                and os.path.isfile(os.path.join(banking_dir, "txn.csv"))):
            skipped = len(HOP_CASES)
            print(f"SKIP: account.csv / txn.csv not found; {skipped} hop cases skipped")
        else:
            hop_header, hop_rows = build_hop_table(banking_dir)
            data_dir = os.path.join(tmp, "hop_data")
            work_dir = os.path.join(tmp, "hop_work")
            os.makedirs(data_dir)
            os.makedirs(work_dir)
            write_csv(os.path.join(data_dir, "hop.csv"), hop_header, hop_rows)
            conn = sqlite3.connect(":memory:")
            load_into_sqlite(conn, "hop", hop_header, hop_rows)
            conn.execute("CREATE INDEX hop_src ON hop(account_src_account_id)")
            schemas = {"hop": hop_header}
            print(f"hop table: {len(hop_rows)} rows, {len(hop_header)} columns")
            for name, from_items, conds in HOP_CASES:
                ok = check_case(sgx_app, data_dir, work_dir, conn, schemas, name, from_items, conds)
                passed, failed = passed + ok, failed + (not ok)
            conn.close()

    print(f"\nSUMMARY: {passed} passed, {failed} failed, {skipped} skipped")
    if failed:
        print("RESULT: FAILED")
        sys.exit(1)
    print("RESULT: ALL TESTS PASSED")


if __name__ == "__main__":
    main()
