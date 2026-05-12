#!/usr/bin/env python3
"""
One-hop thread scaling experiment (Banking 1M).

Compares two systems on the same Banking 1M dataset, varying the thread
count for both:

  1. ours       -- obligraph/build/banking_onehop  (--threads N)
  2. obliviator -- obl-radix/baselines/obliviatorFK-TDX/obliviator_1hop_chained
                   (positional <num_threads> argument)

Per (system, threads) cell: 1 warm-up (discarded) + 1 measurement run.
Strictly sequential -- full machine for every run. Cells are interleaved
by thread count: at each thread count we run (ours warm-up, ours measured,
obliviator warm-up, obliviator measured) before moving to the next thread
count.  This averages out any machine drift across the sweep.

Outputs (under results/one_hop_thread_scaling/):
  breakdown.csv          -- every stage of every run (warm-up + measured)
  breakdown_summary.csv  -- measured run only
  headline.csv           -- per-thread headline + obliviator/ours speedup
  run_metadata.json      -- commit, host, nproc, build flags, settings
  binary_stdout.log      -- full stdout from every invocation

Headline metric:
  ours       -> sum of in_wall_clock=1 stages with category=ONLINE
                (matches the binary's TIMING_REPORTED total for --report ONLINE)
  obliviator -> step 1 (total) + build intermediate + step 2 (total)
                (matches the binary's printed "OBLIVIOUS WORK")

The per-run output CSVs (the actual one-hop result rows) are written to
a tempdir and removed when the script exits -- only the timing artefacts
above are kept.

Usage:
  python3 scripts/experiments/run_one_hop_thread_scaling.py
  python3 scripts/experiments/run_one_hop_thread_scaling.py --skip-build
  python3 scripts/experiments/run_one_hop_thread_scaling.py --threads 1 4 16
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

PROJECT_DIR = Path(__file__).resolve().parents[2]

# Our one-hop
OURS_BUILD_DIR = PROJECT_DIR / "obligraph" / "build"
OURS_BIN = OURS_BUILD_DIR / "banking_onehop"
OURS_DATA = PROJECT_DIR / "input" / "plaintext" / "banking_1M"

# Obliviator chained one-hop
OBLIV_DIR = PROJECT_DIR / "obl-radix" / "baselines" / "obliviatorFK-TDX"
OBLIV_BIN = OBLIV_DIR / "obliviator_1hop_chained"
OBLIV_SRC = OBLIV_DIR / "work_1M" / "src.txt"

RESULTS_DIR = PROJECT_DIR / "results" / "one_hop_thread_scaling"

THREADS = [1, 2, 4, 8, 16, 32, 64, 120]


# === ours: parsers (reused from scripts/run_onehop_scaling.py) ===

HEADER_RE = re.compile(r"^=== TIMING BREAKDOWN ===")
END_RE = re.compile(r"^---\s*Category totals")
CATEGORY_RE = re.compile(r"^\[(ONLINE|OFFLINE|IO)\]\s*$")


def parse_stage_line(line):
    """One stage line -> (name, in_wall_clock, ms) or None."""
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


def parse_ours_breakdown(stdout):
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
        name, in_wc, ms = parsed
        rows.append((name, category, ms, in_wc))
    return rows


# === obliviator: parser ===
#
# The chained driver prints a "=== 1-Hop Summary (chained) ===" block of
# free-text "label : <seconds> s" lines, plus an "off-clock CSV I/O" line
# with a different shape.  We map labels to (stage, category, in_wall_clock)
# so the rows align with the schema used for ours.
#
# OBLIVIOUS WORK is recorded as a synthetic diagnostic (in_wall_clock=0)
# -- it equals step1(total) + build intermediate + step2(total), so summing
# the wall-clock-contributing stages produces the same number without
# double-counting.

OBLIV_HEADER_RE = re.compile(r"=== 1-Hop Summary \(chained\) ===")
OBLIV_LINE_RE = re.compile(
    r"^\s*([A-Za-z][A-Za-z0-9 ()+\-\[\].]+?)\s*:\s*([0-9.]+)\s*s(?:\s|$)"
)
OBLIV_IO_RE = re.compile(
    r"off-clock CSV I/O\s*:\s*load\s*([0-9.]+),\s*write\s*([0-9.]+)"
)

OBLIV_STAGE_MAP = {
    # label                       -> (stage_name,             cat,      in_wall_clock)
    "step 1 (sort)":               ("step 1 (sort)",          "ONLINE", 0),
    "step 1 (total)":              ("step 1 (total)",         "ONLINE", 1),
    "build intermediate":          ("build intermediate",     "ONLINE", 1),
    "step 2 (sort)":               ("step 2 (sort)",          "ONLINE", 0),
    "step 2 (total)":              ("step 2 (total)",         "ONLINE", 1),
    "emit (in-memory)":            ("emit (in-memory)",       "ONLINE", 0),
    "OBLIVIOUS WORK":              ("OBLIVIOUS WORK",         "ONLINE", 0),
    "OBLIVIOUS WORK + EMIT":       ("OBLIVIOUS WORK + EMIT",  "ONLINE", 0),
}


def parse_obliv_breakdown(stdout):
    rows = []
    in_summary = False
    for line in stdout.splitlines():
        if OBLIV_HEADER_RE.search(line):
            in_summary = True
            continue
        if not in_summary:
            continue
        m = OBLIV_LINE_RE.match(line)
        if m:
            key = m.group(1).strip()
            if key in OBLIV_STAGE_MAP:
                ms = float(m.group(2)) * 1000.0
                name, cat, in_wc = OBLIV_STAGE_MAP[key]
                rows.append((name, cat, ms, in_wc))
            continue
        mio = OBLIV_IO_RE.search(line)
        if mio:
            rows.append(("CSV load",  "IO", float(mio.group(1)) * 1000.0, 0))
            rows.append(("CSV write", "IO", float(mio.group(2)) * 1000.0, 0))
    return rows


# === build ===

def build_ours():
    print(f"[build] cmake configure {OURS_BUILD_DIR} (Release, -O3 -DNDEBUG)")
    OURS_BUILD_DIR.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "cmake",
            "-S", str(PROJECT_DIR / "obligraph"),
            "-B", str(OURS_BUILD_DIR),
            "-DCMAKE_BUILD_TYPE=Release",
        ],
        check=True, cwd=PROJECT_DIR,
    )
    print(f"[build] cmake --build {OURS_BUILD_DIR} --target banking_onehop")
    subprocess.run(
        [
            "cmake", "--build", str(OURS_BUILD_DIR),
            "--config", "Release",
            "--target", "banking_onehop",
            "--parallel",
        ],
        check=True, cwd=PROJECT_DIR,
    )
    if not OURS_BIN.exists():
        sys.exit(f"[build] FAILED -- {OURS_BIN} missing after build")


def build_obliviator():
    print(f"[build] make obliviator_1hop_chained "
          f"(Makefile.standalone CFLAGS already include -O3 -march=native)")
    subprocess.run(
        ["make", "-f", "Makefile.standalone", "obliviator_1hop_chained"],
        check=True, cwd=OBLIV_DIR,
    )
    if not OBLIV_BIN.exists():
        sys.exit(f"[build] FAILED -- {OBLIV_BIN} missing after build")


# === run ===

def run_ours(threads, tmpdir):
    out_csv = Path(tmpdir) / f"ours_t{threads}_{int(time.time()*1000)}.csv"
    proc = subprocess.run(
        [
            str(OURS_BIN), str(OURS_DATA), str(out_csv),
            "--threads", str(threads),
        ],
        check=True, cwd=PROJECT_DIR, capture_output=True, text=True,
    )
    rows = parse_ours_breakdown(proc.stdout)
    if not rows:
        sys.stderr.write(proc.stdout[-4000:])
        sys.exit(f"[run]  ours: failed to parse TIMING BREAKDOWN (threads={threads})")
    return rows, proc.stdout


def run_obliviator(threads, tmpdir):
    out_csv = Path(tmpdir) / f"obliv_t{threads}_{int(time.time()*1000)}.csv"
    proc = subprocess.run(
        [str(OBLIV_BIN), str(threads), str(OBLIV_SRC), str(out_csv)],
        check=True, cwd=PROJECT_DIR, capture_output=True, text=True,
    )
    rows = parse_obliv_breakdown(proc.stdout)
    if not rows:
        sys.stderr.write(proc.stdout[-4000:])
        sys.exit(f"[run]  obliviator: failed to parse summary (threads={threads})")
    return rows, proc.stdout


# === orchestration ===

def online_wall_clock_ms(rows):
    return sum(ms for (_, cat, ms, in_wc) in rows
               if cat == "ONLINE" and in_wc)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--skip-build", action="store_true",
                    help="don't rebuild either binary")
    ap.add_argument("--threads", type=int, nargs="+", default=None,
                    help=f"override thread sweep (default: {THREADS})")
    args = ap.parse_args()

    threads_list = args.threads if args.threads else THREADS

    if not args.skip_build:
        build_ours()
        build_obliviator()
    else:
        for b in (OURS_BIN, OBLIV_BIN):
            if not b.exists():
                sys.exit(f"--skip-build but {b} missing")

    if not OURS_DATA.is_dir():
        sys.exit(f"missing dataset: {OURS_DATA}")
    if not OBLIV_SRC.is_file():
        sys.exit(f"missing obliviator src: {OBLIV_SRC}")

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    raw_path  = RESULTS_DIR / "breakdown.csv"
    sum_path  = RESULTS_DIR / "breakdown_summary.csv"
    head_path = RESULTS_DIR / "headline.csv"
    log_path  = RESULTS_DIR / "binary_stdout.log"

    raw_cols = ["system", "threads", "run_id", "is_warmup",
                "stage", "category", "time_ms", "in_wall_clock"]
    sum_cols = ["system", "threads", "stage", "category",
                "time_ms", "in_wall_clock"]

    tmpdir = tempfile.mkdtemp(prefix="onehop_thread_scaling_")
    log_fh = open(log_path, "w")
    try:
        with open(raw_path, "w", newline="") as raw_f, \
             open(sum_path, "w", newline="") as sum_f:
            raw_w = csv.writer(raw_f); raw_w.writerow(raw_cols)
            sum_w = csv.writer(sum_f); sum_w.writerow(sum_cols)

            # measured-run headline: dict[threads] -> {"ours": ms, "obliviator": ms}
            headline = {}

            for t in threads_list:
                print(f"\n[run]  threads={t}: ours then obliviator "
                      f"(1 warmup + 1 measured each)")
                for system, runner in (("ours", run_ours),
                                       ("obliviator", run_obliviator)):
                    for run_id, is_warmup in ((1, True), (2, False)):
                        tag = "warmup" if is_warmup else "measured"
                        t0 = time.time()
                        rows, stdout = runner(t, tmpdir)
                        wall = time.time() - t0

                        log_fh.write(
                            f"\n\n========== {system} threads={t} "
                            f"run={run_id} ({tag}) ==========\n"
                        )
                        log_fh.write(stdout)
                        log_fh.flush()

                        online_wc = online_wall_clock_ms(rows)
                        print(
                            f"[run]  {system:<10} t={t:<3} run={run_id} ({tag:<8}) "
                            f"wall={wall:6.2f}s  "
                            f"ONLINE-wallclock={online_wc:9.2f}ms  "
                            f"({len(rows)} stages)"
                        )

                        for stage, cat, ms, in_wc in rows:
                            raw_w.writerow([
                                system, t, run_id, int(is_warmup),
                                stage, cat, f"{ms:.3f}", int(in_wc),
                            ])
                            if not is_warmup:
                                sum_w.writerow([
                                    system, t, stage, cat,
                                    f"{ms:.3f}", int(in_wc),
                                ])

                        if not is_warmup:
                            headline.setdefault(t, {})[system] = online_wc

                raw_f.flush()
                sum_f.flush()

        # headline CSV: per-thread ours / obliviator + speedup
        with open(head_path, "w", newline="") as hf:
            hw = csv.writer(hf)
            hw.writerow([
                "threads",
                "ours_online_ms",
                "obliviator_oblivious_work_ms",
                "speedup_obliv_over_ours",
            ])
            for t in sorted(headline.keys()):
                ours = headline[t].get("ours")
                obliv = headline[t].get("obliviator")
                if ours and obliv:
                    speedup = f"{obliv / ours:.3f}"
                else:
                    speedup = ""
                hw.writerow([
                    t,
                    f"{ours:.3f}" if ours is not None else "",
                    f"{obliv:.3f}" if obliv is not None else "",
                    speedup,
                ])

        commit = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=PROJECT_DIR, capture_output=True, text=True,
        ).stdout.strip()
        branch = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=PROJECT_DIR, capture_output=True, text=True,
        ).stdout.strip()
        meta = {
            "commit": commit,
            "branch": branch,
            "hostname": platform.node(),
            "uname": platform.platform(),
            "nproc": os.cpu_count(),
            "build_type": "Release",
            "ours_build_flags": "-O3 -DNDEBUG (cmake Release)",
            "obliviator_build_flags":
                "-O3 -march=native -mno-avx512f -pthread "
                "(Makefile.standalone CFLAGS)",
            "threads": threads_list,
            "warmup_runs_per_cell": 1,
            "measured_runs_per_cell": 1,
            "ordering": "interleaved by thread count "
                        "(ours warmup, ours measured, obliv warmup, obliv measured "
                        "at each t before advancing)",
            "dataset": "Banking W1, 1M accounts (5M txns)",
            "ours_data_dir": str(OURS_DATA),
            "obliviator_src": str(OBLIV_SRC),
            "headline_metric": (
                "ours = sum of in_wall_clock=1 stages with category=ONLINE; "
                "obliviator = step 1 (total) + build intermediate + step 2 (total) "
                "(matches printed OBLIVIOUS WORK)."
            ),
            "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        (RESULTS_DIR / "run_metadata.json").write_text(json.dumps(meta, indent=2))

    finally:
        log_fh.close()
        shutil.rmtree(tmpdir, ignore_errors=True)

    print(f"\n[done] raw     : {raw_path}")
    print(f"[done] summary : {sum_path}")
    print(f"[done] headline: {head_path}")
    print(f"[done] stdout  : {log_path}")
    print(f"[done] metadata: {RESULTS_DIR / 'run_metadata.json'}")


if __name__ == "__main__":
    main()
