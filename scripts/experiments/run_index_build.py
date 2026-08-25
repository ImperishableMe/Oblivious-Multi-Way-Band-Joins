#!/usr/bin/env python3
"""
Index-build (OFFLINE) cost across all six paper datasets, at n=3.

What it measures
----------------
The one-hop pipeline pays a query-independent OFFLINE cost before any query
runs.  For every dataset in tab:datasets we report the per-stage OFFLINE
breakdown emitted by the one-hop driver:

  buildNodeIndex        the oblivious node index over the node table, sized by
                        the public edge count.
  index copy (src)      deep copy of the built index (probing is destructive,
                        so each side needs its own copy).
  initProbeSide (wall)  concurrent wall-clock of the two per-side probe
                        scaffolds (the (src)/(dst) entries are diagnostic).

TWO indexes are needed -- one per probe side -- so the reported index-build cost
is 2 x buildNodeIndex (INDEX_COUNT below).  The driver builds one index and deep
-copies it for the second side, so a measured run contains a single
`buildNodeIndex` stage; the reported number doubles it and drops the deep copy,
which exists only because of that reuse.  summary.csv keeps both: the reported
`index_build_*` (doubled) and the raw `build_once_*`.

`offline_*` is the reported OFFLINE wall-clock total, 2 x buildNodeIndex +
initProbeSide (wall).  `offline_measured_*` is what the binary itself totalled
for the OFFLINE category in a run (one build + deep copy + scaffolds), kept for
transparency.  Diagnostic stages (marked `*` by the binary) are recorded in
raw_runs.csv with in_wall_clock=0 and never summed.

ONLINE stages are parsed and stored too, so the table can put the one-time
OFFLINE cost next to the recurring per-query ONLINE cost, but the experiment
exists for the OFFLINE column.

Protocol
--------
Per dataset: 1 discarded warm-up run + 3 measured runs (`--reps`), strictly
sequential, one process per run, full machine each time.  Datasets are run
smallest-edge-count first so failures surface fast.

The warm-up matters more here than elsewhere: `buildNodeIndex` searches hash
strategies on a cold planner and caches the winning plan in
obligraph/build/hash_map.bin72 (cit-Patents takes ~16 min cold, seconds warm),
and a dataset read for the first time also pays cold page cache.  The measured
runs therefore report the *warm-planner* index build, which is what every other
experiment in this series measures.  The warm-up rows stay in raw_runs.csv, so
a cold/warm gap is visible rather than hidden.

The one-hop result CSV is written to /dev/null: this experiment keeps only
timings, and hi_large's hop table is ~180M rows.  CSV read/write is category IO
and excluded from both totals regardless.

Outputs (under results/index_build/)
------------------------------------
  raw_runs.csv        every stage of every run (warm-up rows included)
  summary.csv         one row per dataset: median/mean/min/max/stddev of the
                      reported index build (2 x buildNodeIndex) and of the
                      OFFLINE wall-clock total, the raw single-build numbers,
                      and the per-stage OFFLINE means
  stages.csv          one row per (dataset, stage): median/mean/stddev across
                      the measured runs, both categories
  index_build.tex     LaTeX table (tab:index_build) for the paper
  run_metadata.json   commit, host, nproc, threads, reps, dataset paths
  binary_stdout.log   full stdout of every invocation

Usage
-----
  python3 scripts/experiments/run_index_build.py
  python3 scripts/experiments/run_index_build.py --skip-build
  python3 scripts/experiments/run_index_build.py --datasets hi_small patents
  python3 scripts/experiments/run_index_build.py --reps 3 --threads 64
  python3 scripts/experiments/run_index_build.py --summarize-only   # re-derive
                                                  # the tables from raw_runs.csv
"""

import argparse
import csv
import json
import platform
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
OBLIGRAPH_SRC = PROJECT_DIR / "obligraph"
OBLIGRAPH_BUILD = OBLIGRAPH_SRC / "build"
DEFAULT_DATA_ROOT = PROJECT_DIR / "input" / "plaintext"
DEFAULT_OUTPUT_DIR = PROJECT_DIR / "results" / "index_build"

# One-hop driver per workload (same mapping as run_e3_cross_dataset.py).
WORKLOADS = {
    "banking": {"bin": "banking_onehop"},
    "aml":     {"bin": "ibm_aml_onehop"},
    "snap":    {"bin": "snap_patents_onehop"},
    "snb":     {"bin": "ldbc_snb_onehop"},
}

# The six datasets of tab:datasets, smallest edge count first.
# hi_large uses the column-trimmed slim txn table (see e2 slim path); the index
# is built over the node table and sized by edge count, neither of which the
# column trim changes.
DATASETS = [
    # Smoke-test entry, excluded from the default sweep ("default": False).
    {"label": "banking_10k", "workload": "banking", "dir_name": "banking_10k",
     "paper_name": "Banking 10k (smoke)", "nodes": 10_000, "edges": 50_000,
     "default": False},
    {"label": "banking_1M", "workload": "banking", "dir_name": "banking_1M",
     "paper_name": "Banking (synthetic)", "nodes": 1_000_000, "edges": 5_000_000},
    {"label": "hi_small", "workload": "aml", "dir_name": "ibm_aml_hi_small",
     "paper_name": "IBM AML HI-Small", "nodes": 515_088, "edges": 5_078_345},
    {"label": "snb_sf30", "workload": "snb", "dir_name": "ldbc_snb_sf30",
     "paper_name": "LDBC SNB (SF30)", "nodes": 165_430, "edges": 12_035_314},
    {"label": "patents", "workload": "snap", "dir_name": "snap_patents",
     "paper_name": "SNAP cit-Patents", "nodes": 3_774_768, "edges": 16_518_948},
    {"label": "hi_medium", "workload": "aml", "dir_name": "ibm_aml_hi_medium",
     "paper_name": "IBM AML HI-Medium", "nodes": 2_077_023, "edges": 31_898_238},
    {"label": "hi_large", "workload": "aml", "dir_name": "ibm_aml_hi_large_slim",
     "paper_name": "IBM AML HI-Large", "nodes": 2_116_168, "edges": 179_702_229},
]

BUILD_STAGE = "buildNodeIndex"
PROBE_INIT_STAGE = "initProbeSide (wall)"

# The pipeline needs one node index per probe side (src and dst), because
# probing is destructive.  A measured run builds ONE index and deep-copies it
# for the second side, so the binary reports a single `buildNodeIndex`.  The
# reported cost builds both from scratch: index build = 2 x buildNodeIndex, and
# the deep copy the driver used in its place drops out of the OFFLINE total.
# `build_once_*` in summary.csv keeps the raw single-build measurement.
INDEX_COUNT = 2

# ---------------------------------------------------------------------------
# Stdout parsers (breakdown parser shared with run_one_hop_thread_scaling.py)
# ---------------------------------------------------------------------------

HEADER_RE = re.compile(r"^=== TIMING BREAKDOWN ===")
END_RE = re.compile(r"^---\s*Category totals")
CATEGORY_RE = re.compile(r"^\[(ONLINE|OFFLINE|IO)\]\s*$")
HASH_STRATEGY_RE = re.compile(r"^hash strategy:\s*(.+?)\s*$", re.MULTILINE)
RESULT_ROWS_RE = re.compile(r"Result:\s+(\d+)\s+rows")


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


def parse_breakdown(stdout):
    """-> list of (stage, category, ms, in_wall_clock)."""
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


# ---------------------------------------------------------------------------
# Build / run
# ---------------------------------------------------------------------------

def build_binaries(targets, log_file):
    print(f"[build] cmake configure {OBLIGRAPH_BUILD} (Release)")
    OBLIGRAPH_BUILD.mkdir(parents=True, exist_ok=True)
    with open(log_file, "a") as fh:
        subprocess.run(
            ["cmake", "-S", str(OBLIGRAPH_SRC), "-B", str(OBLIGRAPH_BUILD),
             "-DCMAKE_BUILD_TYPE=Release"],
            check=True, cwd=PROJECT_DIR, stdout=fh, stderr=subprocess.STDOUT,
        )
        for target in sorted(targets):
            print(f"[build] cmake --build --target {target}")
            subprocess.run(
                ["cmake", "--build", str(OBLIGRAPH_BUILD), "--config", "Release",
                 "--target", target, "--parallel"],
                check=True, cwd=PROJECT_DIR, stdout=fh, stderr=subprocess.STDOUT,
            )


def run_onehop(binary, data_dir, threads, timeout_s, log_fh, tag):
    """One invocation. Returns (stage rows, wall_s); the full stdout goes to the
    log, where load_aux() picks up the hash strategy and the hop row count."""
    cmd = [str(binary), str(data_dir), "/dev/null",
           "--report", "OFFLINE,ONLINE", "--threads", str(threads)]
    t0 = time.time()
    proc = subprocess.run(cmd, cwd=PROJECT_DIR, capture_output=True,
                          text=True, timeout=timeout_s)
    wall = time.time() - t0
    log_fh.write(f"\n{'='*78}\n### {tag}\n### cmd: {' '.join(cmd)}\n"
                 f"### wall: {wall:.1f}s  rc={proc.returncode}\n{'='*78}\n")
    log_fh.write(proc.stdout)
    if proc.stderr:
        log_fh.write("\n--- stderr ---\n" + proc.stderr)
    log_fh.flush()
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout[-4000:] + "\n" + proc.stderr[-4000:] + "\n")
        raise RuntimeError(f"{binary.name} exited {proc.returncode} ({tag})")
    rows = parse_breakdown(proc.stdout)
    if not rows:
        sys.stderr.write(proc.stdout[-4000:])
        raise RuntimeError(f"could not parse TIMING BREAKDOWN ({tag})")
    return rows, wall


# ---------------------------------------------------------------------------
# Stats helpers
# ---------------------------------------------------------------------------

def stats(values):
    """-> (median, mean, min, max, stddev). stddev of a single sample = 0.0."""
    if not values:
        return ("", "", "", "", "")
    return (
        statistics.median(values),
        statistics.fmean(values),
        min(values),
        max(values),
        statistics.stdev(values) if len(values) > 1 else 0.0,
    )


def fmt_ms(v):
    return "" if v == "" else f"{v:.3f}"


def latex_seconds(mean_ms, stddev_ms):
    """mean +/- stddev in seconds, 2 decimals (3 for sub-100ms values)."""
    mean_s, sd_s = mean_ms / 1000.0, stddev_ms / 1000.0
    prec = 3 if mean_s < 0.1 else 2
    return f"${mean_s:.{prec}f} \\pm {sd_s:.{prec}f}$"


# ---------------------------------------------------------------------------
# Aggregation (also reachable standalone via --summarize-only)
# ---------------------------------------------------------------------------

def load_measured(raw_path):
    """raw_runs.csv -> {label: {"stages": {stage: [ms per measured run]},
                               "meta": {stage: (category, in_wall_clock)}}}"""
    per_dataset = {}
    with open(raw_path, newline="") as f:
        for r in csv.DictReader(f):
            if r["is_warmup"] == "1":
                continue
            entry = per_dataset.setdefault(
                r["dataset"], {"stages": {}, "meta": {}, "threads": r["threads"]})
            entry["stages"].setdefault(r["stage"], []).append(float(r["time_ms"]))
            entry["meta"][r["stage"]] = (r["category"], r["in_wall_clock"] == "1")
    return per_dataset


def load_aux(log_path):
    """binary_stdout.log -> {label: {"strategy": str, "hop_rows": str}}, taken
    from the last measured run of each dataset. The log is self-describing: every
    invocation is preceded by a '### <label> run i/n (measured|warmup)' marker."""
    aux = {}
    if not log_path.exists():
        return aux
    marker = re.compile(r"^### (\S+) run \d+/\d+ \((measured|warmup)\)\s*$")
    label, measured_run = None, False
    for line in log_path.read_text(errors="replace").splitlines():
        m = marker.match(line)
        if m:
            label, measured_run = m.group(1), m.group(2) == "measured"
            continue
        if label is None or not measured_run:
            continue
        ms = HASH_STRATEGY_RE.match(line)
        if ms:
            aux.setdefault(label, {})["strategy"] = ms.group(1)
            continue
        mr = RESULT_ROWS_RE.search(line)
        if mr:
            aux.setdefault(label, {})["hop_rows"] = mr.group(1)
    return aux


def aggregate(out_dir, datasets, args):
    """Derive stages.csv, summary.csv and index_build.tex from raw_runs.csv.

    Reported index-build cost is INDEX_COUNT x buildNodeIndex: the pipeline needs
    one index per probe side, and a measured run builds one and deep-copies it.
    See the INDEX_COUNT comment at the top of this file.
    """
    raw_path = out_dir / "raw_runs.csv"
    if not raw_path.exists():
        sys.exit(f"missing {raw_path} -- run the sweep before summarizing")
    per_dataset = load_measured(raw_path)
    aux = load_aux(out_dir / "binary_stdout.log")

    missing = [d["label"] for d in datasets if d["label"] not in per_dataset]
    if missing:
        sys.exit(f"{raw_path} has no measured rows for: {', '.join(missing)}")

    # --- stages.csv: every stage, both categories -------------------------
    stages_path = out_dir / "stages.csv"
    with open(stages_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["dataset", "nodes", "edges", "stage", "category",
                    "in_wall_clock", "n_runs", "median_ms", "mean_ms",
                    "min_ms", "max_ms", "stddev_ms"])
        for d in datasets:
            entry = per_dataset[d["label"]]
            for stage, samples in entry["stages"].items():
                category, in_wc = entry["meta"][stage]
                med, mean, lo, hi, sd = stats(samples)
                w.writerow([d["label"], d["nodes"], d["edges"], stage, category,
                            int(in_wc), len(samples), fmt_ms(med), fmt_ms(mean),
                            fmt_ms(lo), fmt_ms(hi), fmt_ms(sd)])

    # --- summary.csv: one row per dataset ---------------------------------
    summary_path = out_dir / "summary.csv"
    summary_rows = []
    with open(summary_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(
            ["dataset", "paper_name", "workload", "nodes", "edges", "threads",
             "n_runs",
             "index_build_median_ms", "index_build_mean_ms", "index_build_min_ms",
             "index_build_max_ms", "index_build_stddev_ms",
             "build_once_median_ms", "build_once_mean_ms", "build_once_stddev_ms",
             "index_copy_mean_ms", "init_probe_wall_mean_ms",
             "offline_median_ms", "offline_mean_ms", "offline_min_ms",
             "offline_max_ms", "offline_stddev_ms",
             "offline_measured_mean_ms", "offline_measured_stddev_ms",
             "online_median_ms", "online_mean_ms", "online_stddev_ms",
             "index_count", "hash_strategy", "hop_rows"])
        for d in datasets:
            entry = per_dataset[d["label"]]
            samples, meta = entry["stages"], entry["meta"]
            build_samples = samples.get(BUILD_STAGE, [])
            n_runs = len(build_samples)
            probe_samples = samples.get(PROBE_INIT_STAGE, [0.0] * n_runs)

            # Per-run category totals: sum the wall-clock-contributing stages of
            # that category within each run, then take stats across runs.
            def category_totals(category):
                per_run = []
                for i in range(n_runs):
                    total = 0.0
                    for stage, vals in samples.items():
                        cat, in_wc = meta[stage]
                        if cat == category and in_wc and i < len(vals):
                            total += vals[i]
                    per_run.append(total)
                return per_run

            # Reported OFFLINE: both indexes built from scratch, so the deep copy
            # the driver uses in its place drops out.
            offline_two_index = [INDEX_COUNT * b + p
                                 for b, p in zip(build_samples, probe_samples)]

            build_once = stats(build_samples)
            index_build = stats([INDEX_COUNT * b for b in build_samples])
            offline = stats(offline_two_index)
            offline_measured = stats(category_totals("OFFLINE"))
            online = stats(category_totals("ONLINE"))
            copy_mean = stats(samples.get("index copy (src)", []))[1]
            probe_mean = stats(probe_samples)[1]

            w.writerow(
                [d["label"], d["paper_name"], d["workload"], d["nodes"], d["edges"],
                 entry["threads"], n_runs,
                 fmt_ms(index_build[0]), fmt_ms(index_build[1]), fmt_ms(index_build[2]),
                 fmt_ms(index_build[3]), fmt_ms(index_build[4]),
                 fmt_ms(build_once[0]), fmt_ms(build_once[1]), fmt_ms(build_once[4]),
                 fmt_ms(copy_mean), fmt_ms(probe_mean),
                 fmt_ms(offline[0]), fmt_ms(offline[1]), fmt_ms(offline[2]),
                 fmt_ms(offline[3]), fmt_ms(offline[4]),
                 fmt_ms(offline_measured[1]), fmt_ms(offline_measured[4]),
                 fmt_ms(online[0]), fmt_ms(online[1]), fmt_ms(online[4]),
                 INDEX_COUNT,
                 aux.get(d["label"], {}).get("strategy", ""),
                 aux.get(d["label"], {}).get("hop_rows", "")])
            summary_rows.append((d, index_build, offline, online, n_runs))

    # --- index_build.tex --------------------------------------------------
    tex_path = out_dir / "index_build.tex"
    n_runs_seen = summary_rows[0][4] if summary_rows else 0
    threads_seen = per_dataset[datasets[0]["label"]]["threads"] if datasets else "?"
    with open(tex_path, "w") as f:
        f.write("% Generated by scripts/experiments/run_index_build.py\n")
        f.write(f"% Mean +/- stddev over {n_runs_seen} measured runs "
                f"({args.warmup_runs} discarded warm-up),\n")
        f.write(f"% one-hop driver at {threads_seen} threads. Seconds.\n")
        f.write(f"% Index build = {INDEX_COUNT} x measured buildNodeIndex "
                "(one index per probe side).\n")
        f.write("\\begin{table}[t]\n\\centering\n\\begin{tabular}{lrrrr}\n\\toprule\n")
        f.write("\\textbf{Dataset} & \\textbf{Nodes} & \\textbf{Edges} & "
                "\\textbf{Index build (s)} & \\textbf{Offline total (s)} \\\\\n")
        f.write("\\midrule\n")
        for d, index_build, offline, _online, _n in summary_rows:
            nodes = f"{d['nodes']/1e6:.1f}M" if d["nodes"] >= 1e6 else f"{d['nodes']/1e3:.0f}K"
            edges = f"{d['edges']/1e6:.1f}M" if d["edges"] >= 1e6 else f"{d['edges']/1e3:.0f}K"
            f.write(f"{d['paper_name']} & {nodes} & {edges} & "
                    f"{latex_seconds(index_build[1], index_build[4])} & "
                    f"{latex_seconds(offline[1], offline[4])} \\\\\n")
        f.write("\\bottomrule\n\\end{tabular}\n")
        f.write("\\caption{One-time index-build cost. ``Index build'' is the "
                "oblivious node index, built once per probe side "
                f"({INDEX_COUNT} indexes); ``Offline total'' adds the per-side "
                "probe scaffolds. Mean $\\pm$ stddev over "
                f"{n_runs_seen} runs.}}\n")
        f.write("\\label{tab:index_build}\n\\end{table}\n")

    return stages_path, summary_path, tex_path


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--datasets", nargs="+", default=None,
                    help="subset of: " + ", ".join(d["label"] for d in DATASETS))
    ap.add_argument("--reps", type=int, default=3,
                    help="measured runs per dataset (default: 3)")
    ap.add_argument("--warmup-runs", type=int, default=1,
                    help="discarded runs before the measured ones (default: 1)")
    ap.add_argument("--threads", type=int, default=64,
                    help="one-hop thread count (default: 64, matches E3)")
    ap.add_argument("--cell-timeout", type=int, default=14400,
                    help="per-run timeout in seconds (default: 4h)")
    ap.add_argument("--skip-build", action="store_true",
                    help="use the one-hop binaries as they are on disk")
    ap.add_argument("--summarize-only", action="store_true",
                    help="re-derive stages.csv / summary.csv / index_build.tex "
                         "from an existing raw_runs.csv; measures nothing")
    ap.add_argument("--data-root", type=Path, default=DEFAULT_DATA_ROOT,
                    help=f"dataset root (default: {DEFAULT_DATA_ROOT})")
    ap.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR,
                    help=f"results dir (default: {DEFAULT_OUTPUT_DIR})")
    args = ap.parse_args()

    known = {d["label"]: d for d in DATASETS}
    if args.datasets:
        unknown = [d for d in args.datasets if d not in known]
        if unknown:
            sys.exit(f"unknown dataset(s): {', '.join(unknown)}\n"
                     f"known: {', '.join(known)}")
        datasets = [known[label] for label in
                    sorted(args.datasets, key=lambda l: known[l]["edges"])]
    else:
        datasets = [d for d in DATASETS if d.get("default", True)]

    if args.reps < 1:
        sys.exit("--reps must be >= 1")

    out_dir = args.output_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / "binary_stdout.log"

    if args.summarize_only:
        stages_path, summary_path, tex_path = aggregate(out_dir, datasets, args)
        # Keep the provenance in step with the tables it describes.
        meta_path = out_dir / "run_metadata.json"
        if meta_path.exists():
            meta = json.loads(meta_path.read_text())
            meta["index_count"] = INDEX_COUNT
            meta["resummarized_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
            meta_path.write_text(json.dumps(meta, indent=2) + "\n")
        print(f"[summarize-only] re-derived from {out_dir / 'raw_runs.csv'}")
        for p in (stages_path, summary_path, tex_path):
            print(f"  {p}")
        return

    log_path.write_text("")

    if not args.skip_build:
        build_binaries({WORKLOADS[d["workload"]]["bin"] for d in datasets}, log_path)

    # Pre-flight: binaries and datasets must exist before a multi-hour sweep.
    for d in datasets:
        binary = OBLIGRAPH_BUILD / WORKLOADS[d["workload"]]["bin"]
        if not binary.exists():
            sys.exit(f"missing one-hop binary: {binary} (drop --skip-build?)")
        data_dir = args.data_root / d["dir_name"]
        for csv_name in ("account.csv", "txn.csv"):
            if not (data_dir / csv_name).exists():
                sys.exit(f"missing dataset file: {data_dir / csv_name}")

    raw_path = out_dir / "raw_runs.csv"
    raw_cols = ["dataset", "workload", "nodes", "edges", "threads", "run_id",
                "is_warmup", "stage", "category", "time_ms", "in_wall_clock"]

    started = time.time()
    with open(raw_path, "w", newline="") as raw_f, open(log_path, "a") as log_fh:
        raw_w = csv.writer(raw_f)
        raw_w.writerow(raw_cols)

        for d in datasets:
            label = d["label"]
            binary = OBLIGRAPH_BUILD / WORKLOADS[d["workload"]]["bin"]
            data_dir = args.data_root / d["dir_name"]
            total_runs = args.warmup_runs + args.reps

            print(f"\n[{label}] {d['nodes']:,} nodes / {d['edges']:,} edges "
                  f"-- {binary.name} on {data_dir.name}, "
                  f"{args.warmup_runs} warm-up + {args.reps} measured")

            for run_id in range(1, total_runs + 1):
                is_warmup = run_id <= args.warmup_runs
                tag = (f"{label} run {run_id}/{total_runs} "
                       f"({'warmup' if is_warmup else 'measured'})")
                print(f"  [{label}] run {run_id}/{total_runs} "
                      f"({'warmup' if is_warmup else 'measured'})...", end="", flush=True)
                rows, wall = run_onehop(
                    binary, data_dir, args.threads, args.cell_timeout, log_fh, tag)

                for stage, category, ms, in_wc in rows:
                    raw_w.writerow([label, d["workload"], d["nodes"], d["edges"],
                                    args.threads, run_id, int(is_warmup),
                                    stage, category, f"{ms:.3f}", int(in_wc)])
                raw_f.flush()

                build_ms = next((ms for stage, _, ms, _ in rows
                                 if stage == BUILD_STAGE), None)
                print(f" wall={wall:.1f}s buildNodeIndex="
                      f"{'n/a' if build_ms is None else f'{build_ms:.1f}ms'}")

    # Tables are derived from the CSV that was just written, so the same code
    # path serves a fresh sweep and a later --summarize-only.
    stages_path, summary_path, tex_path = aggregate(out_dir, datasets, args)

    # --- run_metadata.json ------------------------------------------------
    def git(*cmd):
        try:
            return subprocess.run(["git", *cmd], cwd=PROJECT_DIR, check=True,
                                  capture_output=True, text=True).stdout.strip()
        except subprocess.CalledProcessError:
            return "unknown"

    metadata = {
        "experiment": "index_build",
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "elapsed_s": round(time.time() - started, 1),
        "git_commit": git("rev-parse", "HEAD"),
        "git_branch": git("rev-parse", "--abbrev-ref", "HEAD"),
        "hostname": platform.node(),
        "nproc": len(__import__("os").sched_getaffinity(0)),
        "threads": args.threads,
        "measurement_runs": args.reps,
        "warmup_runs": args.warmup_runs,
        "cell_timeout_s": args.cell_timeout,
        "data_root": str(args.data_root),
        "build": "skipped" if args.skip_build else "cmake Release (-O3 -DNDEBUG)",
        "index_count": INDEX_COUNT,
        "hash_plan_cache": str(OBLIGRAPH_BUILD / "hash_map.bin72"),
        "datasets": [
            {"label": d["label"], "dir": str(args.data_root / d["dir_name"]),
             "binary": WORKLOADS[d["workload"]]["bin"],
             "nodes": d["nodes"], "edges": d["edges"]}
            for d in datasets
        ],
        "notes": (
            "Reported index build = index_count x the measured buildNodeIndex: "
            "one index per probe side, where the driver builds one and deep-copies "
            "it. Result CSV written to /dev/null (IO category, excluded from totals). "
            "Measured runs are warm-planner: buildNodeIndex caches its hash plan "
            "in hash_map.bin72, and the discarded warm-up pays any cold-plan or "
            "cold-page-cache cost. Warm-up rows are kept in raw_runs.csv."
        ),
    }
    (out_dir / "run_metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")

    print(f"\n[done] {time.time() - started:.0f}s total")
    print(f"  {raw_path}")
    print(f"  {stages_path}")
    print(f"  {summary_path}")
    print(f"  {tex_path}")
    print(f"  {out_dir / 'run_metadata.json'}")


if __name__ == "__main__":
    main()
