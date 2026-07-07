#!/usr/bin/env python3
"""
E4 query-shape runner (IBM AML W4, one dataset scale).

Where E1 varies chain *depth* and E2 varies *data scale*, E4 varies the
*topology* of the join graph at a fixed dataset. The headline contrast is the
size-matched 4-edge quartet — 4-hop chain, fan-in, fan-out, tree — all
9-relation queries with one anchor filter (a bank), differing only in shape:

  chain    a1 -t1-> a2 -t2-> a3 -t3-> a4 -t4-> a5      (anchor a1)
  fan-in   a1..a4 -t1..t4-> a5  (hub receives)         (anchor a5, the hub)
  fan-out  a1 -t1..t4-> a2..a5  (hub sends)            (anchor a1, the hub)
  tree     a1 -t1-> a2 { -t2-> a3 ; -t3-> a4 -t4-> a5 }(anchor a1, the root)

2-hop and 3-hop chains are included as context (they replicate E1 cells).

Systems (the three canonical ones; the filtered full_mwj is not run):

  1. nebuladb (label as "Graphite" in plots)
       one-hop table (once per rep) -> rewrite_chain_query.py decomposition
       -> sgx_app on the rewritten query over the hop table. The rewriter's
       BFS decomposition handles branching topologies, so the SAME pipeline
       serves every shape: fan-in becomes a 4-way self-join of the hop table
       on account_dest_account_id, fan-out on account_src_account_id, tree a
       mix. Per-cell latency = onehop_ms (amortized, inherited from the
       per-rep one-hop run) + mwj_ms.
  2. full_mwj_no_filter ("Full MWJ")
       sgx_app --no-filter directly on the shape query.
  3. obliviator_chained
       obliviator_khop_chained on the chain queries only (its kernel is a
       path pipeline); non-chain shapes are recorded UNSUPPORTED — the
       chained-kernel baseline structurally cannot express them.

Failure handling: any per-cell OOM/crash/timeout is recorded (output_rows =
OOM/TIMEOUT) and never aborts the sweep. For the two unfiltered systems
(obliviator_chained, full_mwj_no_filter) whose output grows with edge count,
a failure at E edges skips every query with >= E edges (SKIPPED).

Correctness is checked separately by tests/test_e4_shape_correctness.py.

Outputs (under results/e4_query_shapes/):
  raw_runs.csv          every run, including warm-ups
  summary.csv           measurement runs only, per cell: n, median, min, max, stddev, output_rows
  run_metadata.json     commit, host, nproc, settings
  binary_stdout.log     full stdout from every invocation
  decomposed/           the rewritten per-shape SQL actually run by nebuladb

Usage:
  python3 scripts/experiments/run_e4_query_shapes.py
  python3 scripts/experiments/run_e4_query_shapes.py --skip-build --cell-timeout 900
  python3 scripts/experiments/run_e4_query_shapes.py --queries aml_fanin,aml_fanout
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
RESULTS_DIR = PROJECT_DIR / "results" / "e4_query_shapes"

SGX_APP = PROJECT_DIR / "sgx_app"
OBLIGRAPH_BUILD = PROJECT_DIR / "obligraph" / "build"
ONEHOP_BIN = OBLIGRAPH_BUILD / "ibm_aml_onehop"
ONEHOP_TARGET = "ibm_aml_onehop"
REWRITER = PROJECT_DIR / "scripts" / "rewrite_chain_query.py"
QUERY_DIR = PROJECT_DIR / "input" / "queries"
DATA_ROOT = PROJECT_DIR / "input" / "plaintext"

OBL_NFK_DIR = PROJECT_DIR / "obl-radix" / "baselines" / "obliviatorNFK-TDX"
OBL_KHOP_BIN = OBL_NFK_DIR / "obliviator_khop_chained"
CONVERT_AML_1HOP = PROJECT_DIR / "obl-radix" / "baselines" / "obliviatorFK-TDX" / "convert_aml_1hop.py"

DEFAULT_DATASET = "ibm_aml_hi_small"

# query -> (shape, edge count). Chains carry their hop count as K for the
# obliviator baseline; non-chain shapes have no K (obliviator can't run them).
QUERY_SHAPES = {
    "aml_2hop":   ("chain", 2),
    "aml_3hop":   ("chain", 3),
    "aml_4hop":   ("chain", 4),
    "aml_fanin":  ("fanin", 4),
    "aml_fanout": ("fanout", 4),
    "aml_tree":   ("tree", 4),
}
DEFAULT_QUERIES = list(QUERY_SHAPES)

DEFAULT_SYSTEMS = ["nebuladb", "full_mwj_no_filter", "obliviator_chained"]
# Systems whose output grows with edge count (they compute the *unfiltered*
# join): once one fails at E edges, every query with >= E edges is skipped.
UNFILTERED_SYSTEMS = {"obliviator_chained", "full_mwj_no_filter"}


# ---------------------------------------------------------------------------
# Stdout parsers (same formats as E1)
# ---------------------------------------------------------------------------

ONEHOP_TIMING_RE = re.compile(r"TIMING_REPORTED\s+categories=\S+\s+total=([\d.]+)ms")
MWJ_TIMING_RE    = re.compile(r"PHASE_TIMING:[^\n]*Total=([\d.]+)")
RESULT_ROWS_RE   = re.compile(r"Result:\s+(\d+)\s+rows")
OBL_KHOP_TIME_RE = re.compile(r"^\s*online_sec\s+\(sum on-clock\)\s+:\s+([\d.]+)", re.MULTILINE)
OBL_KHOP_ROWS_RE = re.compile(r"^\s*final_rows\s+:\s+(\d+)", re.MULTILINE)


def parse_onehop_total_ms(stdout: str) -> float:
    m = ONEHOP_TIMING_RE.search(stdout)
    if not m:
        raise RuntimeError("could not find TIMING_REPORTED line in one-hop output")
    return float(m.group(1))


def parse_mwj_total_ms(stdout: str) -> float:
    m = MWJ_TIMING_RE.search(stdout)
    if not m:
        raise RuntimeError("could not find PHASE_TIMING line in sgx_app output")
    return float(m.group(1)) * 1000.0


def parse_result_rows(stdout: str) -> int:
    matches = RESULT_ROWS_RE.findall(stdout)
    if not matches:
        raise RuntimeError("could not find 'Result: N rows' line")
    return int(matches[-1])


def parse_obliviator_total_ms(stdout: str) -> float:
    m = OBL_KHOP_TIME_RE.search(stdout)
    if not m:
        raise RuntimeError("could not find 'online_sec  (sum on-clock)' line in obliviator output")
    return float(m.group(1)) * 1000.0


def parse_obliviator_rows(stdout: str) -> int:
    m = OBL_KHOP_ROWS_RE.search(stdout)
    if not m:
        raise RuntimeError("could not find 'final_rows' line in obliviator output")
    return int(m.group(1))


# ---------------------------------------------------------------------------
# Subprocess helpers
# ---------------------------------------------------------------------------

class CellTimeout(RuntimeError):
    """Raised when a cell's subprocess exceeds the per-cell wall-clock budget.
    A RuntimeError subclass so the OOM/crash handler catches it too, while
    letting a timeout be recorded as TIMEOUT rather than OOM."""


def run_capture(cmd, *, log_file, env_extra=None, timeout=None) -> str:
    log_file.write(f"\n+++ {' '.join(str(c) for c in cmd)}\n")
    if env_extra:
        log_file.write(f"    env_extra: {env_extra}\n")
    if timeout:
        log_file.write(f"    timeout: {timeout}s\n")
    log_file.flush()
    proc_env = None
    if env_extra:
        proc_env = os.environ.copy()
        proc_env.update({str(k): str(v) for k, v in env_extra.items()})
    try:
        proc = subprocess.run(
            [str(c) for c in cmd], capture_output=True, text=True, env=proc_env,
            timeout=timeout)
    except subprocess.TimeoutExpired as e:
        partial = e.stdout or ""
        if isinstance(partial, bytes):
            partial = partial.decode(errors="replace")
        log_file.write(partial)
        log_file.write(f"\n!!! TIMEOUT after {timeout}s: "
                       f"{' '.join(str(c) for c in cmd)}\n")
        log_file.flush()
        raise CellTimeout(f"timed out after {timeout}s") from None
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
# Stage runners
# ---------------------------------------------------------------------------

def run_onehop(data_dir: Path, out_dir: Path, threads: int, log_file) -> tuple:
    """Run the one-hop driver, place hop.csv in out_dir. Returns (ms, rows)."""
    hop_csv = out_dir / "hop.csv"
    stdout = run_capture(
        [ONEHOP_BIN, data_dir, hop_csv, "--threads", str(threads)],
        log_file=log_file,
    )
    return parse_onehop_total_ms(stdout), parse_result_rows(stdout)


def run_rewrite(query_path: Path, out_path: Path, log_file):
    """Untimed: decompose the shape query against the hop table."""
    run_capture(["python3", REWRITER, query_path, out_path], log_file=log_file)


def run_mwj(query_path: Path, data_dir: Path, mwj_threads: int, log_file,
            no_filter: bool = False, timeout=None) -> tuple:
    """Run sgx_app once. Returns (mwj_ms, output_rows)."""
    env_extra = None
    if mwj_threads and mwj_threads > 0:
        env_extra = {"OBL_MWJ_SORT_THREADS": str(mwj_threads)}
    with tempfile.TemporaryDirectory() as tmp:
        out_csv = Path(tmp) / "out.csv"
        cmd = [SGX_APP, query_path, data_dir, out_csv]
        if no_filter:
            cmd.append("--no-filter")
        stdout = run_capture(cmd, log_file=log_file, env_extra=env_extra,
                             timeout=timeout)
    return parse_mwj_total_ms(stdout), parse_result_rows(stdout)


def generate_obliviator_src_txt(data_dir: Path, out_dir: Path, log_file) -> Path:
    src_path = out_dir / "src.txt"
    dst_path = out_dir / "dst.txt"
    run_capture(
        ["python3", CONVERT_AML_1HOP,
         data_dir / "account.csv", data_dir / "txn.csv", src_path, dst_path],
        log_file=log_file,
    )
    return src_path


def run_obliviator(K: int, src_txt: Path, threads: int, log_file,
                   timeout=None) -> tuple:
    """Run obliviator_khop_chained (all E4 chains have K >= 2)."""
    with tempfile.TemporaryDirectory() as tmp:
        out_csv = Path(tmp) / "obl_out.csv"
        cmd = [OBL_KHOP_BIN, str(threads), str(K), src_txt, out_csv]
        stdout = run_capture(cmd, log_file=log_file, timeout=timeout)
    return parse_obliviator_total_ms(stdout), parse_obliviator_rows(stdout)


# ---------------------------------------------------------------------------
# Build helpers
# ---------------------------------------------------------------------------

def build_binaries(log_file):
    print(f"[build] obligraph/{ONEHOP_TARGET} (Release)...", flush=True)
    run_capture(
        ["cmake", "--build", OBLIGRAPH_BUILD,
         "--target", ONEHOP_TARGET, "--config", "Release"],
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

    print("[build] obliviator_khop_chained (NFK)...", flush=True)
    run_capture(
        ["make", "-C", OBL_NFK_DIR, "-f", "Makefile.standalone",
         "obliviator_khop_chained"],
        log_file=log_file,
    )


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
        "binaries": {
            "sgx_app": str(SGX_APP),
            "onehop": str(ONEHOP_BIN),
            "obliviator_khop_chained": str(OBL_KHOP_BIN),
            "obliviator_converter": str(CONVERT_AML_1HOP),
        },
        "query_shapes": {q: {"shape": s, "edges": e}
                         for q, (s, e) in QUERY_SHAPES.items()},
        "note": (
            "E4 varies join-graph topology at one dataset scale; the 4-edge "
            "quartet (4-hop chain, fan-in, fan-out, tree) is size-matched. "
            "one-hop runs once per repetition and its cost is inherited by "
            "every nebuladb cell in that rep. obliviator_chained only runs "
            "chain shapes (its kernel is a path pipeline); non-chain shapes "
            "are recorded UNSUPPORTED. For the unfiltered systems "
            "(obliviator_chained, full_mwj_no_filter) a failure at E edges "
            "skips all queries with >= E edges (SKIPPED)."
        ),
    }


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dataset", default=DEFAULT_DATASET,
                   help=f"Dataset name under input/plaintext/ "
                        f"(default: {DEFAULT_DATASET}).")
    p.add_argument("--queries", default=",".join(DEFAULT_QUERIES),
                   help=f"Comma-separated query names "
                        f"(default: {','.join(DEFAULT_QUERIES)}).")
    p.add_argument("--systems", default=",".join(DEFAULT_SYSTEMS),
                   help=f"Comma-separated systems (default and allowed: "
                        f"{','.join(DEFAULT_SYSTEMS)}).")
    p.add_argument("--measurement-runs", type=int, default=1,
                   help="Recorded measurement runs per cell (default: 1)")
    p.add_argument("--warmup-runs", type=int, default=1,
                   help="Discarded warm-up runs per cell (default: 1)")
    p.add_argument("--onehop-threads", type=int, default=64,
                   help="Threads passed to the one-hop driver (default: 64).")
    p.add_argument("--mwj-threads", type=int, default=64,
                   help="Workers in sgx_app's shared bitonic parallel_sort "
                        "thread pool via OBL_MWJ_SORT_THREADS (default: 64; "
                        "0 = leave unset).")
    p.add_argument("--obliviator-threads", type=int, default=64,
                   help="Threads passed to obliviator_khop_chained "
                        "(default: 64). At threads>=2 the NFK kernel has a "
                        "documented pairing bug (rowcount correct, tuple "
                        "identities scrambled) — this baseline is perf-only.")
    p.add_argument("--cell-timeout", type=int, default=0,
                   help="Per-cell wall-clock budget in seconds for each MWJ / "
                        "obliviator run (default: 0 = no limit). A cell over "
                        "budget is recorded as TIMEOUT and, for the unfiltered "
                        "systems, skips queries with >= that edge count.")
    p.add_argument("--obliviator-timeout", type=int, default=None,
                   help="Separate per-cell budget (seconds) for "
                        "obliviator_chained only (default: inherit "
                        "--cell-timeout; 0 = no limit).")
    p.add_argument("--skip-build", action="store_true",
                   help="Skip binary rebuild step")
    p.add_argument("--output-dir", default=str(RESULTS_DIR),
                   help=f"Output directory (default: {RESULTS_DIR})")
    args = p.parse_args()

    queries = args.queries.split(",")
    systems = args.systems.split(",")
    cell_timeout = args.cell_timeout or None  # 0 -> None (no limit)
    obliviator_timeout = (cell_timeout if args.obliviator_timeout is None
                          else (args.obliviator_timeout or None))
    data_dir = DATA_ROOT / args.dataset
    if not data_dir.is_dir():
        sys.exit(f"dataset not found: {data_dir}")
    for q in queries:
        if q not in QUERY_SHAPES:
            sys.exit(f"unknown query: {q} (allowed: {list(QUERY_SHAPES)})")
        if not (QUERY_DIR / f"{q}.sql").is_file():
            sys.exit(f"query not found: {QUERY_DIR}/{q}.sql")
    for s in systems:
        if s not in DEFAULT_SYSTEMS:
            sys.exit(f"unknown system: {s} (allowed: {DEFAULT_SYSTEMS})")

    needs_onehop = "nebuladb" in systems
    needs_obliviator = "obliviator_chained" in systems and any(
        QUERY_SHAPES[q][0] == "chain" for q in queries)

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / "binary_stdout.log"
    raw_csv_path = out_dir / "raw_runs.csv"
    summary_csv_path = out_dir / "summary.csv"
    meta_path = out_dir / "run_metadata.json"

    meta = collect_metadata(args)

    print(f"E4 runner: dataset={args.dataset}")
    print(f"  queries: {queries}")
    print(f"  systems: {systems}")
    print(f"  warm-up: {args.warmup_runs}   measurement: {args.measurement_runs}")
    print(f"  mwj_env: {meta['mwj_env'] or '(unset — sgx_app uses hardware_concurrency)'}")
    print(f"  output : {out_dir}")
    print()

    rows = []
    total_reps = args.warmup_runs + args.measurement_runs

    # Per-system smallest edge count at which an unfiltered system failed;
    # queries with >= that many edges are then SKIPPED for it.
    failed_at_edges = {}

    with open(log_path, "w") as log_file:
        if not args.skip_build:
            build_binaries(log_file)

        # Pre-compute decomposed SQL once per query (deterministic, untimed).
        decomposed_dir = out_dir / "decomposed"
        decomposed_dir.mkdir(exist_ok=True)
        decomposed_for = {}
        if needs_onehop:
            for q in queries:
                dpath = decomposed_dir / f"{q}.sql"
                run_rewrite(QUERY_DIR / f"{q}.sql", dpath, log_file)
                decomposed_for[q] = dpath

        with tempfile.TemporaryDirectory() as tmp_root:
            tmp_root = Path(tmp_root)
            hop_dir = tmp_root / "hop_data"
            hop_dir.mkdir()

            obliviator_src_txt = None
            if needs_obliviator:
                obl_dir = tmp_root / "obliviator_input"
                obl_dir.mkdir()
                print(f"[setup] generating obliviator src.txt from {data_dir} ...",
                      flush=True)
                t0 = time.time()
                obliviator_src_txt = generate_obliviator_src_txt(
                    data_dir, obl_dir, log_file)
                print(f"  -> {obliviator_src_txt} ({time.time()-t0:.1f}s wall)")

            for rep_idx in range(total_reps):
                is_warmup = rep_idx < args.warmup_runs
                run_id = rep_idx - args.warmup_runs + 1  # 1..N when measured
                label = "warm" if is_warmup else f"run{run_id}"
                print(f"--- rep {rep_idx+1}/{total_reps} ({label}) ---", flush=True)

                # 1. one-hop, once per rep (hop.csv reused by every nebuladb cell).
                onehop_ms, onehop_rows = (None, None)
                if needs_onehop:
                    print(f"  one-hop ...", end="", flush=True)
                    t0 = time.time()
                    onehop_ms, onehop_rows = run_onehop(
                        data_dir, hop_dir, args.onehop_threads, log_file)
                    print(f" total={onehop_ms:.1f}ms rows={onehop_rows} "
                          f"({time.time()-t0:.1f}s wall)")

                # 2. Per-query cells.
                for query in queries:
                    qpath = QUERY_DIR / f"{query}.sql"
                    shape, edges = QUERY_SHAPES[query]
                    for system in systems:
                        cell_total_ms = None
                        cell_onehop_ms = ""
                        cell_mwj_ms = ""
                        cell_rows = None

                        if system == "obliviator_chained" and shape != "chain":
                            # The chained kernel is a path pipeline; it cannot
                            # express branching topologies.
                            cell_rows = "UNSUPPORTED"
                        elif (system in failed_at_edges
                              and edges >= failed_at_edges[system]):
                            cell_rows = "SKIPPED"
                        else:
                            # OOM-tolerant: a crash / OOM-kill / timeout is
                            # recorded as a failed cell; the sweep continues.
                            try:
                                if system == "nebuladb":
                                    mwj_ms, mwj_rows = run_mwj(
                                        decomposed_for[query], hop_dir,
                                        args.mwj_threads, log_file,
                                        timeout=cell_timeout)
                                    cell_total_ms = onehop_ms + mwj_ms
                                    cell_onehop_ms = onehop_ms  # inherited from this rep
                                    cell_mwj_ms = mwj_ms
                                    cell_rows = mwj_rows
                                elif system == "full_mwj_no_filter":
                                    mwj_ms, mwj_rows = run_mwj(
                                        qpath, data_dir, args.mwj_threads,
                                        log_file, no_filter=True,
                                        timeout=cell_timeout)
                                    cell_total_ms = mwj_ms
                                    cell_mwj_ms = mwj_ms
                                    cell_rows = mwj_rows
                                elif system == "obliviator_chained":
                                    obl_ms, obl_rows = run_obliviator(
                                        edges, obliviator_src_txt,
                                        args.obliviator_threads, log_file,
                                        timeout=obliviator_timeout)
                                    cell_total_ms = obl_ms
                                    cell_rows = obl_rows
                            except RuntimeError as e:
                                kind = "TIMEOUT" if isinstance(e, CellTimeout) else "OOM"
                                log_file.write(f"\n!!! {system} {kind} at {query}: {e}\n")
                                log_file.flush()
                                if system in UNFILTERED_SYSTEMS:
                                    failed_at_edges[system] = min(
                                        failed_at_edges.get(system, edges), edges)
                                cell_rows = kind
                                cell_total_ms = None

                        total_str = (f"{cell_total_ms:.1f}ms" if cell_total_ms is not None
                                     else str(cell_rows))
                        print(f"  [{query}] {system:20s} {label} -> total={total_str}"
                              f" rows={cell_rows}", flush=True)
                        rows.append({
                            "system": system,
                            "query": query,
                            "shape": shape,
                            "edges": edges,
                            "dataset": args.dataset,
                            "run_id": run_id,
                            "is_warmup": int(is_warmup),
                            "total_ms": cell_total_ms,
                            "onehop_ms": cell_onehop_ms,
                            "mwj_ms": cell_mwj_ms,
                            "output_rows": cell_rows,
                        })

    # raw_runs.csv (everything)
    fieldnames = ["system", "query", "shape", "edges", "dataset", "run_id",
                  "is_warmup", "total_ms", "onehop_ms", "mwj_ms", "output_rows"]
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
            "system", "query", "shape", "edges", "dataset", "n_runs",
            "median_ms", "min_ms", "max_ms", "stddev_ms", "output_rows",
        ])
        sw.writeheader()
        for (system, query, dataset), cell in sorted(by_cell.items()):
            shape, edges = QUERY_SHAPES[query]
            totals = [c["total_ms"] for c in cell if c["total_ms"] is not None]
            n = len(totals)
            if n == 0:
                sw.writerow({
                    "system": system, "query": query, "shape": shape,
                    "edges": edges, "dataset": dataset, "n_runs": 0,
                    "median_ms": "", "min_ms": "", "max_ms": "", "stddev_ms": "",
                    "output_rows": cell[0]["output_rows"],
                })
                continue
            sw.writerow({
                "system": system, "query": query, "shape": shape,
                "edges": edges, "dataset": dataset, "n_runs": n,
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
