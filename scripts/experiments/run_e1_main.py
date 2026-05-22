#!/usr/bin/env python3
"""
E1 main-result runner (Banking, chain queries only, reduced scope).

Compares two systems on the five Banking chain queries at a single
dataset scale:

  1. NebulaDB
       - 1-hop  : `banking_onehop` alone (its output IS the 1-hop join).
       - >= 2-hop : rewrite the chain query against the pre-built hop
                    table -> `sgx_app` on the rewritten query.
     Per-cell latency = `mwj_ms` for the rewritten-query run, plus the
     amortized one-hop cost (inherited from the per-rep one-hop run).
  2. Full MWJ
       - `sgx_app` directly on the chain query (no decomposition).
     Per-cell latency = `mwj_ms`.

The one-hop step is run **once per measurement repetition**, NOT per cell:
the hop table is a per-dataset constant, identical regardless of which
chain query we run on top of it. Running it inside every cell would just
re-do the same FK join. The single per-rep run gives both:
  - the `onehop_ms` value inherited by every >= 2-hop NebulaDB cell, and
  - the latency reported for the 1-hop NebulaDB cell itself.

Per (system, query) cell: `--warmup-runs` discarded + `--measurement-runs`
recorded. Loop order: outer = repetition, inner = (query, system).
This keeps the one-hop matched run-by-run with the MWJ measurements it's
paired with. Strictly sequential; full machine for every run.

Correctness is checked separately by `tests/test_e1_chain_correctness.py`.

Outputs (under results/e1_main/):
  raw_runs.csv          every run, including warm-ups
  summary.csv           measurement runs only, per cell: n, median, min, max, stddev, output_rows
  run_metadata.json     commit, host, nproc, build flags, settings
  binary_stdout.log     full stdout from every invocation

Usage:
  python3 scripts/experiments/run_e1_main.py
  python3 scripts/experiments/run_e1_main.py --dataset banking_5k --skip-build
  python3 scripts/experiments/run_e1_main.py --measurement-runs 5
  python3 scripts/experiments/run_e1_main.py --queries banking_1hop,banking_3hop
"""

import argparse
import csv
import json
import os
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
RESULTS_DIR = PROJECT_DIR / "results" / "e1_main"

SGX_APP = PROJECT_DIR / "sgx_app"
ONEHOP_BIN = PROJECT_DIR / "obligraph" / "build" / "banking_onehop"
REWRITER = PROJECT_DIR / "scripts" / "rewrite_chain_query.py"
QUERY_DIR = PROJECT_DIR / "input" / "queries"
DATA_ROOT = PROJECT_DIR / "input" / "plaintext"

ALL_QUERIES = ["banking_1hop", "banking_2hop", "banking_3hop", "banking_4hop", "banking_5hop"]
ALL_SYSTEMS = ["nebuladb", "full_mwj"]


# ---------------------------------------------------------------------------
# Stdout parsers
# ---------------------------------------------------------------------------

# banking_onehop:  "TIMING_REPORTED categories=ONLINE total=110.298ms"
ONEHOP_TIMING_RE = re.compile(r"TIMING_REPORTED\s+categories=\S+\s+total=([\d.]+)ms")
# sgx_app:         "PHASE_TIMING: Bottom-Up=... Total=0.047904"  (seconds)
MWJ_TIMING_RE    = re.compile(r"PHASE_TIMING:[^\n]*Total=([\d.]+)")
# both:            "Result: 2 rows" (sgx_app); "Result: 1000 rows, 11 columns" (banking_onehop)
RESULT_ROWS_RE   = re.compile(r"Result:\s+(\d+)\s+rows")


def parse_onehop_total_ms(stdout: str) -> float:
    m = ONEHOP_TIMING_RE.search(stdout)
    if not m:
        raise RuntimeError("could not find TIMING_REPORTED line in banking_onehop output")
    return float(m.group(1))


def parse_mwj_total_ms(stdout: str) -> float:
    """PHASE_TIMING Total (seconds) -> ms."""
    m = MWJ_TIMING_RE.search(stdout)
    if not m:
        raise RuntimeError("could not find PHASE_TIMING line in sgx_app output")
    return float(m.group(1)) * 1000.0


def parse_result_rows(stdout: str) -> int:
    matches = RESULT_ROWS_RE.findall(stdout)
    if not matches:
        raise RuntimeError("could not find 'Result: N rows' line")
    return int(matches[-1])


def hop_count(query_name: str) -> int:
    return int(query_name.split("_")[1].rstrip("hop"))


# ---------------------------------------------------------------------------
# Subprocess helpers
# ---------------------------------------------------------------------------

def run_capture(cmd, *, log_file, env_extra=None) -> str:
    log_file.write(f"\n+++ {' '.join(str(c) for c in cmd)}\n")
    if env_extra:
        log_file.write(f"    env_extra: {env_extra}\n")
    log_file.flush()
    proc_env = None
    if env_extra:
        proc_env = os.environ.copy()
        proc_env.update({str(k): str(v) for k, v in env_extra.items()})
    proc = subprocess.run(
        [str(c) for c in cmd], capture_output=True, text=True, env=proc_env)
    log_file.write(proc.stdout)
    if proc.stderr:
        log_file.write("\n--- stderr ---\n" + proc.stderr)
    log_file.flush()
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed (exit {proc.returncode}): {' '.join(str(c) for c in cmd)}\n"
            f"--- stderr ---\n{proc.stderr}"
        )
    return proc.stdout


# ---------------------------------------------------------------------------
# Stage runners (each returns just what its caller needs)
# ---------------------------------------------------------------------------

def run_onehop(data_dir: Path, out_dir: Path, threads: int, log_file) -> tuple:
    """Run banking_onehop and place hop.csv in out_dir. Returns (onehop_ms, rows)."""
    hop_csv = out_dir / "hop.csv"
    stdout = run_capture(
        [ONEHOP_BIN, data_dir, hop_csv, "--threads", str(threads)],
        log_file=log_file,
    )
    return parse_onehop_total_ms(stdout), parse_result_rows(stdout)


def run_rewrite(query_path: Path, out_path: Path, log_file):
    """Untimed: rewrite chain query against the hop table."""
    run_capture(["python3", REWRITER, query_path, out_path], log_file=log_file)


def run_mwj(query_path: Path, data_dir: Path, mwj_threads: int, log_file) -> tuple:
    """Run sgx_app once. Returns (mwj_ms, output_rows). Sets
    OBL_MWJ_SORT_THREADS=mwj_threads in the subprocess env to size sgx_app's
    shared bitonic parallel_sort thread pool (read once via magic static in
    app/data_structures/table.cpp). Pass 0 to leave the env unset; sgx_app
    then defaults to std::thread::hardware_concurrency()."""
    env_extra = None
    if mwj_threads and mwj_threads > 0:
        env_extra = {"OBL_MWJ_SORT_THREADS": str(mwj_threads)}
    with tempfile.TemporaryDirectory() as tmp:
        out_csv = Path(tmp) / "out.csv"
        stdout = run_capture(
            [SGX_APP, query_path, data_dir, out_csv],
            log_file=log_file, env_extra=env_extra)
    return parse_mwj_total_ms(stdout), parse_result_rows(stdout)


# ---------------------------------------------------------------------------
# Build helpers
# ---------------------------------------------------------------------------

def build_binaries(log_file):
    print("[build] obligraph/banking_onehop (Release)...", flush=True)
    run_capture(
        ["cmake", "--build", PROJECT_DIR / "obligraph" / "build",
         "--target", "banking_onehop", "--config", "Release"],
        log_file=log_file,
    )
    print("[build] sgx_app (make)...", flush=True)
    proc = subprocess.run(["make"], cwd=PROJECT_DIR, capture_output=True, text=True)
    log_file.write("\n+++ make (sgx_app)\n" + proc.stdout)
    if proc.stderr:
        log_file.write("\n--- stderr ---\n" + proc.stderr)
    log_file.flush()
    if proc.returncode != 0:
        raise RuntimeError(f"sgx_app build failed:\n{proc.stderr}")


# ---------------------------------------------------------------------------
# Main sweep
# ---------------------------------------------------------------------------

def collect_metadata(args) -> dict:
    def sh(cmd):
        try:
            return subprocess.check_output(cmd, cwd=PROJECT_DIR, text=True).strip()
        except Exception:
            return ""
    return {
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "git_commit": sh(["git", "rev-parse", "HEAD"]),
        "git_branch": sh(["git", "rev-parse", "--abbrev-ref", "HEAD"]),
        "hostname": platform.node(),
        "nproc": os.cpu_count(),
        "platform": platform.platform(),
        "python": sys.version.split()[0],
        "args": vars(args),
        "mwj_env": (
            {"OBL_MWJ_SORT_THREADS": str(args.mwj_threads)}
            if args.mwj_threads and args.mwj_threads > 0 else {}
        ),
        "binaries": {"sgx_app": str(SGX_APP), "banking_onehop": str(ONEHOP_BIN)},
        "note": (
            "one-hop runs once per repetition (not per cell); its result is "
            "reused for all >=2-hop NebulaDB cells in that rep and reported "
            "as the latency of the 1-hop NebulaDB cell."
        ),
    }


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dataset", default="banking_1M",
                   help="Dataset name under input/plaintext/ (default: banking_1M)")
    p.add_argument("--queries", default=",".join(ALL_QUERIES),
                   help="Comma-separated query names (default: all five chains)")
    p.add_argument("--systems", default=",".join(ALL_SYSTEMS),
                   help="Comma-separated systems (default: nebuladb,full_mwj)")
    p.add_argument("--measurement-runs", type=int, default=1,
                   help="Recorded measurement runs per cell (default: 1)")
    p.add_argument("--warmup-runs", type=int, default=1,
                   help="Discarded warm-up runs per cell (default: 1)")
    p.add_argument("--onehop-threads", type=int, default=32,
                   help="Threads passed to banking_onehop --threads (default: 32).")
    p.add_argument("--mwj-threads", type=int, default=64,
                   help="Workers in sgx_app's shared bitonic parallel_sort thread "
                        "pool, passed via the OBL_MWJ_SORT_THREADS env var "
                        "(default: 64). Set to 0 to leave the env unset; sgx_app "
                        "then defaults to std::thread::hardware_concurrency().")
    p.add_argument("--skip-build", action="store_true",
                   help="Skip binary rebuild step")
    p.add_argument("--output-dir", default=str(RESULTS_DIR),
                   help=f"Output directory (default: {RESULTS_DIR})")
    args = p.parse_args()

    queries = args.queries.split(",")
    systems = args.systems.split(",")
    data_dir = DATA_ROOT / args.dataset
    if not data_dir.is_dir():
        sys.exit(f"dataset not found: {data_dir}")
    for q in queries:
        if not (QUERY_DIR / f"{q}.sql").is_file():
            sys.exit(f"query not found: {QUERY_DIR}/{q}.sql")
    for s in systems:
        if s not in ALL_SYSTEMS:
            sys.exit(f"unknown system: {s} (allowed: {ALL_SYSTEMS})")

    needs_onehop = "nebuladb" in systems
    needs_mwj = ("full_mwj" in systems) or ("nebuladb" in systems and any(hop_count(q) >= 2 for q in queries))

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / "binary_stdout.log"
    raw_csv_path = out_dir / "raw_runs.csv"
    summary_csv_path = out_dir / "summary.csv"
    meta_path = out_dir / "run_metadata.json"

    meta = collect_metadata(args)

    print(f"E1 runner: dataset={args.dataset}")
    print(f"  queries: {queries}")
    print(f"  systems: {systems}")
    print(f"  warm-up: {args.warmup_runs}   measurement: {args.measurement_runs}")
    print(f"  mwj_env: {meta['mwj_env'] or '(unset — sgx_app uses hardware_concurrency)'}")
    print(f"  output : {out_dir}")
    print()

    rows = []
    total_reps = args.warmup_runs + args.measurement_runs

    with open(log_path, "w") as log_file:
        if not args.skip_build:
            build_binaries(log_file)

        # Pre-compute rewritten SQL once per query (rewriter is deterministic, untimed).
        # These live under out_dir/decomposed/ so they're inspectable after the run.
        decomposed_dir = out_dir / "decomposed"
        decomposed_dir.mkdir(exist_ok=True)
        decomposed_for = {}
        if "nebuladb" in systems:
            for q in queries:
                if hop_count(q) >= 2:
                    dpath = decomposed_dir / f"{q}.sql"
                    run_rewrite(QUERY_DIR / f"{q}.sql", dpath, log_file)
                    decomposed_for[q] = dpath

        with tempfile.TemporaryDirectory() as tmp_root:
            tmp_root = Path(tmp_root)
            hop_dir = tmp_root / "hop_data"
            hop_dir.mkdir()

            for rep_idx in range(total_reps):
                is_warmup = rep_idx < args.warmup_runs
                run_id = rep_idx - args.warmup_runs + 1  # 1..N when measured
                label = "warm" if is_warmup else f"run{run_id}"
                print(f"--- rep {rep_idx+1}/{total_reps} ({label}) ---", flush=True)

                # 1. one-hop, once per rep (gives onehop_ms; produces hop.csv reused below).
                onehop_ms, onehop_rows = (None, None)
                if needs_onehop:
                    print(f"  one-hop ...", end="", flush=True)
                    t0 = time.time()
                    onehop_ms, onehop_rows = run_onehop(data_dir, hop_dir, args.onehop_threads, log_file)
                    print(f" total={onehop_ms:.1f}ms rows={onehop_rows} "
                          f"({time.time()-t0:.1f}s wall)")

                # 2. Per-query cells.
                for query in queries:
                    qpath = QUERY_DIR / f"{query}.sql"
                    hk = hop_count(query)
                    for system in systems:
                        cell_total_ms = None
                        cell_onehop_ms = ""
                        cell_mwj_ms = ""
                        cell_rows = None

                        if system == "nebuladb":
                            if hk == 1:
                                # 1-hop NebulaDB = the per-rep one-hop run.
                                cell_total_ms = onehop_ms
                                cell_onehop_ms = onehop_ms
                                cell_rows = onehop_rows
                            else:
                                # rewrite is already cached; only time the MWJ stage.
                                t0 = time.time()
                                mwj_ms, mwj_rows = run_mwj(
                                    decomposed_for[query], hop_dir,
                                    args.mwj_threads, log_file)
                                wall = time.time() - t0
                                cell_total_ms = onehop_ms + mwj_ms
                                cell_onehop_ms = onehop_ms  # inherited from this rep
                                cell_mwj_ms = mwj_ms
                                cell_rows = mwj_rows
                        elif system == "full_mwj":
                            t0 = time.time()
                            mwj_ms, mwj_rows = run_mwj(
                                qpath, data_dir, args.mwj_threads, log_file)
                            wall = time.time() - t0
                            cell_total_ms = mwj_ms
                            cell_mwj_ms = mwj_ms
                            cell_rows = mwj_rows

                        print(f"  [{query}] {system:9s} {label} -> total={cell_total_ms:.1f}ms"
                              f" rows={cell_rows}", flush=True)
                        rows.append({
                            "system": system,
                            "query": query,
                            "dataset": args.dataset,
                            "run_id": run_id,
                            "is_warmup": int(is_warmup),
                            "total_ms": cell_total_ms,
                            "onehop_ms": cell_onehop_ms,
                            "mwj_ms": cell_mwj_ms,
                            "output_rows": cell_rows,
                        })

    # raw_runs.csv (everything)
    fieldnames = ["system", "query", "dataset", "run_id", "is_warmup",
                  "total_ms", "onehop_ms", "mwj_ms", "output_rows"]
    with open(raw_csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)

    # summary.csv (measurement runs only)
    by_cell = {}
    for r in rows:
        if r["is_warmup"]:
            continue
        by_cell.setdefault((r["system"], r["query"], r["dataset"]), []).append(r)

    with open(summary_csv_path, "w", newline="") as f:
        sw = csv.DictWriter(f, fieldnames=[
            "system", "query", "dataset", "n_runs",
            "median_ms", "min_ms", "max_ms", "stddev_ms", "output_rows",
        ])
        sw.writeheader()
        for (system, query, dataset), cell in sorted(by_cell.items()):
            totals = [c["total_ms"] for c in cell]
            n = len(totals)
            sw.writerow({
                "system": system, "query": query, "dataset": dataset,
                "n_runs": n,
                "median_ms": statistics.median(totals),
                "min_ms": min(totals),
                "max_ms": max(totals),
                "stddev_ms": statistics.stdev(totals) if n >= 2 else 0.0,
                "output_rows": cell[0]["output_rows"],
            })

    meta["ended_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)

    print()
    print(f"raw runs    -> {raw_csv_path}")
    print(f"summary     -> {summary_csv_path}")
    print(f"metadata    -> {meta_path}")
    print(f"stdout log  -> {log_path}")


if __name__ == "__main__":
    main()
