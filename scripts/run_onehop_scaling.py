#!/usr/bin/env python3
"""
One-hop scaling experiment.

For each dataset size (200K / 500K / 1M / 10M accounts), runs
obligraph/build/banking_onehop twice (the first run is a cache warm-up
and is discarded; only run #2 is reported in the summary).

Per-stage timing breakdown is parsed from the binary's "=== TIMING
BREAKDOWN ===" block and written in long format to:

  results/onehop_scaling/breakdown.csv          (both runs)
  results/onehop_scaling/breakdown_summary.csv  (run 2 only)
  results/onehop_scaling/run_metadata.json      (commit, host, build flags)

Usage:
  python3 scripts/run_onehop_scaling.py
  python3 scripts/run_onehop_scaling.py --skip-build --skip-generation
  python3 scripts/run_onehop_scaling.py --sizes 200k          # subset, for smoke tests
"""

import argparse
import csv
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
GENERATOR = PROJECT_DIR / "scripts" / "generate_banking_scaled.py"
BUILD_DIR = PROJECT_DIR / "obligraph" / "build"
ONEHOP_BIN = BUILD_DIR / "banking_onehop"
RESULTS_DIR = PROJECT_DIR / "results" / "onehop_scaling"
DATA_ROOT = PROJECT_DIR / "input" / "plaintext"

SEED = 42
EDGE_RATIO = 5  # txns per account, fixed by generate_banking_scaled.py
NUM_RUNS = 2   # first = warm-up (discarded), second = recorded

# Order matters: smallest to largest so failures surface fast.
SIZES = [
    {"label": "200k",  "accounts":   200_000, "dir_name": "banking_200k"},
    {"label": "500k",  "accounts":   500_000, "dir_name": "banking_500k"},
    {"label": "1M",    "accounts": 1_000_000, "dir_name": "banking_1M"},
    {"label": "10M",   "accounts":10_000_000, "dir_name": "banking_10M"},
]

# Lines we want to capture from the breakdown.
CATEGORY_RE = re.compile(r"^\[(ONLINE|OFFLINE|IO)\]\s*$")
HEADER_RE = re.compile(r"^=== TIMING BREAKDOWN ===")
END_RE = re.compile(r"^---\s*Category totals")


def parse_stage_line(line: str):
    """
    Stage lines look like (from obligraph/include/timer.h):
        '  buildNodeIndex                   1234.56 ms'   (contributes)
        '  src branch (total)              * 1234.56 ms'  (diagnostic)
    Returns (name, in_wall_clock, ms) or None if the line isn't a stage.
    """
    line = line.rstrip()
    if not line.endswith(" ms"):
        return None
    body = line[:-3].rstrip()
    parts = body.rsplit(None, 1)
    if len(parts) != 2:
        return None
    rest, num_str = parts
    try:
        ms = float(num_str)
    except ValueError:
        return None
    rest = rest.rstrip()
    is_diag = rest.endswith("*")
    if is_diag:
        rest = rest[:-1].rstrip()
    name = rest.strip()
    if not name:
        return None
    return name, (not is_diag), ms


def parse_breakdown(stdout: str):
    """Walk the output and yield (stage, category, ms, in_wall_clock)."""
    rows = []
    in_breakdown = False
    category = None
    for line in stdout.splitlines():
        if not in_breakdown:
            if HEADER_RE.search(line):
                in_breakdown = True
            continue
        if END_RE.search(line):
            break
        m = CATEGORY_RE.match(line.strip())
        if m:
            category = m.group(1)
            continue
        if category is None:
            continue
        parsed = parse_stage_line(line)
        if parsed is None:
            continue
        name, in_wall_clock, ms = parsed
        rows.append((name, category, ms, in_wall_clock))
    return rows


def parse_timing_reported(stdout: str):
    """Extract the TIMING_REPORTED total (ms) line for sanity checks."""
    m = re.search(r"TIMING_REPORTED\s+categories=(\S+)\s+total=([\d.]+)ms", stdout)
    if not m:
        return None
    return {"categories": m.group(1), "total_ms": float(m.group(2))}


def build_binary():
    print(f"[build] cmake --build {BUILD_DIR} --target banking_onehop")
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--target", "banking_onehop"],
        check=True,
        cwd=PROJECT_DIR,
    )
    if not ONEHOP_BIN.exists():
        sys.exit(f"[build] FAILED — {ONEHOP_BIN} missing after build")


def dataset_is_valid(data_dir: Path, accounts: int) -> bool:
    """A dataset is 'valid' if the row counts match what seed=42 would produce."""
    acc_csv = data_dir / "account.csv"
    txn_csv = data_dir / "txn.csv"
    own_csv = data_dir / "owner.csv"
    if not (acc_csv.exists() and txn_csv.exists() and own_csv.exists()):
        return False
    # Cheap row-count check; trust seed=42 for content.
    def lc(p): return sum(1 for _ in open(p))
    return (
        lc(acc_csv) == accounts + 1
        and lc(txn_csv) == EDGE_RATIO * accounts + 1
        and lc(own_csv) == accounts // EDGE_RATIO + 1
    )


def generate_dataset(spec):
    data_dir = DATA_ROOT / spec["dir_name"]
    if dataset_is_valid(data_dir, spec["accounts"]):
        print(f"[gen]  {spec['label']:>5}: existing dataset matches expected sizes — skipping")
        return
    print(f"[gen]  {spec['label']:>5}: generating {spec['accounts']:,} accounts → {data_dir}")
    if data_dir.exists():
        shutil.rmtree(data_dir)
    t0 = time.time()
    subprocess.run(
        [
            "python3",
            str(GENERATOR),
            str(spec["accounts"]),
            str(data_dir),
            "--seed",
            str(SEED),
        ],
        check=True,
        cwd=PROJECT_DIR,
    )
    print(f"[gen]  {spec['label']:>5}: done in {time.time() - t0:.1f}s")


def run_onehop(data_dir: Path):
    """Run banking_onehop once; return parsed breakdown rows + reported total."""
    with tempfile.TemporaryDirectory() as tmp:
        out_csv = Path(tmp) / "hop.csv"
        proc = subprocess.run(
            [str(ONEHOP_BIN), str(data_dir), str(out_csv)],
            check=True,
            cwd=PROJECT_DIR,
            capture_output=True,
            text=True,
        )
    rows = parse_breakdown(proc.stdout)
    reported = parse_timing_reported(proc.stdout)
    if not rows:
        sys.stderr.write(proc.stdout[-4000:])
        sys.exit("[run]  FAILED — could not parse TIMING BREAKDOWN block (output above)")
    return rows, reported, proc.stdout


def write_metadata(out_path: Path, sizes_run, build_flags: str):
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=PROJECT_DIR,
        capture_output=True,
        text=True,
    ).stdout.strip()
    branch = subprocess.run(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"],
        cwd=PROJECT_DIR,
        capture_output=True,
        text=True,
    ).stdout.strip()
    meta = {
        "commit": commit,
        "branch": branch,
        "hostname": platform.node(),
        "uname": platform.platform(),
        "nproc": os.cpu_count(),
        "build_type": "Release",
        "build_flags": build_flags,
        "seed": SEED,
        "edge_ratio": EDGE_RATIO,
        "num_runs_per_size": NUM_RUNS,
        "warmup_runs_discarded": 1,
        "sizes": sizes_run,
        "binary": str(ONEHOP_BIN),
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    out_path.write_text(json.dumps(meta, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--skip-build", action="store_true", help="don't rebuild banking_onehop")
    parser.add_argument("--skip-generation", action="store_true", help="don't (re)generate datasets")
    parser.add_argument(
        "--sizes",
        nargs="+",
        default=None,
        help="subset of size labels to run (default: all). e.g. --sizes 200k 500k",
    )
    args = parser.parse_args()

    sizes = SIZES
    if args.sizes:
        wanted = set(args.sizes)
        sizes = [s for s in SIZES if s["label"] in wanted]
        unknown = wanted - {s["label"] for s in sizes}
        if unknown:
            sys.exit(f"unknown size labels: {sorted(unknown)}")

    if not args.skip_build:
        build_binary()
    elif not ONEHOP_BIN.exists():
        sys.exit(f"--skip-build but {ONEHOP_BIN} doesn't exist")

    if not args.skip_generation:
        for spec in sizes:
            generate_dataset(spec)
    else:
        for spec in sizes:
            d = DATA_ROOT / spec["dir_name"]
            if not dataset_is_valid(d, spec["accounts"]):
                sys.exit(f"--skip-generation but dataset {d} is missing or wrong size")

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    raw_path = RESULTS_DIR / "breakdown.csv"
    summary_path = RESULTS_DIR / "breakdown_summary.csv"
    log_path = RESULTS_DIR / "binary_stdout.log"
    log_fh = open(log_path, "w")

    raw_columns = [
        "dataset",
        "num_accounts",
        "num_edges",
        "run_id",
        "is_warmup",
        "stage",
        "category",
        "time_ms",
        "in_wall_clock",
    ]
    # Summary holds only run 2, so run_id / is_warmup are constant and dropped.
    summary_columns = [
        "dataset",
        "num_accounts",
        "num_edges",
        "stage",
        "category",
        "time_ms",
        "in_wall_clock",
        "timing_reported_categories",
        "timing_reported_total_ms",
    ]
    with open(raw_path, "w", newline="") as raw_f, open(summary_path, "w", newline="") as sum_f:
        raw_w = csv.writer(raw_f)
        sum_w = csv.writer(sum_f)
        raw_w.writerow(raw_columns)
        sum_w.writerow(summary_columns)

        for spec in sizes:
            data_dir = DATA_ROOT / spec["dir_name"]
            num_edges = spec["accounts"] * EDGE_RATIO
            print(f"\n[run]  {spec['label']:>5}: {NUM_RUNS} runs (run 1 is warm-up, discarded)")
            for run_id in range(1, NUM_RUNS + 1):
                is_warmup = run_id == 1
                tag = "warmup" if is_warmup else "measured"
                t0 = time.time()
                rows, reported, stdout = run_onehop(data_dir)
                wall = time.time() - t0
                log_fh.write(f"\n\n========== {spec['label']} run {run_id} ({tag}) ==========\n")
                log_fh.write(stdout)
                log_fh.flush()
                rep_total = reported["total_ms"] if reported else float("nan")
                print(
                    f"[run]  {spec['label']:>5}: run {run_id} ({tag:<8}) "
                    f"wall={wall:7.2f}s  TIMING_REPORTED={rep_total:9.2f}ms  "
                    f"({len(rows)} stages)"
                )
                for stage, category, ms, in_wc in rows:
                    raw_w.writerow([
                        spec["dir_name"],
                        spec["accounts"],
                        num_edges,
                        run_id,
                        int(is_warmup),
                        stage,
                        category,
                        f"{ms:.3f}",
                        int(in_wc),
                    ])
                    if not is_warmup:
                        sum_w.writerow([
                            spec["dir_name"],
                            spec["accounts"],
                            num_edges,
                            stage,
                            category,
                            f"{ms:.3f}",
                            int(in_wc),
                            reported["categories"] if reported else "",
                            f"{rep_total:.3f}" if reported else "",
                        ])
            raw_f.flush()
            sum_f.flush()

    log_fh.close()

    # Top-level latency summary: one row per dataset, with online/offline as
    # separate columns. Sums only stages that contribute to wall-clock — diagnostic
    # in-parallel stages are excluded so the totals reflect true elapsed time.
    category_summary_path = RESULTS_DIR / "category_summary.csv"
    with open(summary_path, newline="") as sf:
        sum_rows = list(csv.DictReader(sf))
    cat_totals = {}  # dataset -> {"ONLINE": ms, "OFFLINE": ms}
    cat_meta = {}    # dataset -> (accounts, edges)
    for r in sum_rows:
        if r["category"] not in ("ONLINE", "OFFLINE"):
            continue
        if int(r["in_wall_clock"]) != 1:
            continue
        cat_totals.setdefault(r["dataset"], {"ONLINE": 0.0, "OFFLINE": 0.0})
        cat_totals[r["dataset"]][r["category"]] += float(r["time_ms"])
        cat_meta[r["dataset"]] = (int(r["num_accounts"]), int(r["num_edges"]))
    with open(category_summary_path, "w", newline="") as cf:
        cw = csv.writer(cf)
        cw.writerow(["dataset", "num_accounts", "num_edges", "online_ms", "offline_ms"])
        for ds in sorted(cat_meta.keys(), key=lambda d: cat_meta[d][1]):
            accts, edges = cat_meta[ds]
            cw.writerow([
                ds,
                accts,
                edges,
                f"{cat_totals[ds]['ONLINE']:.3f}",
                f"{cat_totals[ds]['OFFLINE']:.3f}",
            ])

    # Sorted-by-time view: drop IO, sort stages within each dataset by time desc.
    # Useful for spotting where the bulk of time is spent at each scale.
    breakdown_sorted_path = RESULTS_DIR / "breakdown_sorted.csv"
    by_ds = {}
    ds_meta = {}
    for r in sum_rows:
        if r["category"] == "IO":
            continue
        by_ds.setdefault(r["dataset"], []).append(r)
        ds_meta[r["dataset"]] = (int(r["num_accounts"]), int(r["num_edges"]))
    with open(breakdown_sorted_path, "w", newline="") as bf:
        bw = csv.writer(bf)
        bw.writerow(["dataset", "num_accounts", "num_edges", "rank", "stage", "category", "time_ms", "in_wall_clock"])
        for ds in sorted(ds_meta.keys(), key=lambda d: ds_meta[d][1]):
            stages = sorted(by_ds[ds], key=lambda r: float(r["time_ms"]), reverse=True)
            for rank, r in enumerate(stages, 1):
                bw.writerow([
                    r["dataset"],
                    r["num_accounts"],
                    r["num_edges"],
                    rank,
                    r["stage"],
                    r["category"],
                    r["time_ms"],
                    r["in_wall_clock"],
                ])

    write_metadata(
        RESULTS_DIR / "run_metadata.json",
        sizes_run=[s["label"] for s in sizes],
        build_flags="-O3 -DNDEBUG",
    )

    print(f"\n[done] raw     : {raw_path}")
    print(f"[done] summary : {summary_path}")
    print(f"[done] category: {category_summary_path}")
    print(f"[done] sorted  : {breakdown_sorted_path}")
    print(f"[done] stdout  : {log_path}")
    print(f"[done] metadata: {RESULTS_DIR / 'run_metadata.json'}")


if __name__ == "__main__":
    main()
