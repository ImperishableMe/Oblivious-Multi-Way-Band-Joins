#!/usr/bin/env python3
"""
Merge the E7 n=3 invocation directories into one summary.

The E7 sweep was measured at n=3 across four runner invocations rather than
one: the runner rewrites raw_runs.csv and summary.csv from scratch on every
call, so a single output directory cannot accumulate points measured at
different times. The four live under results/e7_selectivity_n3/:

  .              s = 0.001, 0.01, 0.05, 0.1   both systems
  s065/          s = 0.65                     both systems
  mid/           s = 0.25, 0.5, 0.55, 0.6     both systems
  hi_nebuladb/   s = 0.7, 0.75                Graphite only (Full MWJ OOMs
                                              at 0.7, a known failure)

This script re-derives summary_all.csv from those raw_runs.csv files without
re-measuring, so a presentation change costs nothing. It adds two columns the
runner does not emit: `mean_ms` (the paper quotes means) and `range_pct`
(min-max as a percentage of the mean, which is how E3/E4/E5 reported spread).

The three failing cells carry no timing to average. They are copied from the
July sweep's summary.csv, where they were observed, and marked
`source=july_<date>` so a reader can tell measured cells from asserted ones.

Usage:
  python3 scripts/experiments/merge_e7_n3.py
  python3 scripts/experiments/merge_e7_n3.py --n3-dir results/e7_selectivity_n3
"""

import argparse
import csv
import statistics
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]

# Invocation subdirectories, in ascending selectivity order. "" is the parent
# directory itself, which holds the first (low-selectivity) invocation.
SUBDIRS = ["", "s065", "mid", "hi_nebuladb"]

FIELDS = ["system", "s_target", "s_achieved", "theta", "expected_rows",
          "n_runs", "mean_ms", "median_ms", "min_ms", "max_ms", "stddev_ms",
          "range_pct", "output_rows", "rows_match", "source"]


def load_measured(n3_dir: Path):
    """Collect measured runs per (system, s_target) across the invocations."""
    runs, meta = {}, {}
    for sub in SUBDIRS:
        path = (n3_dir / sub / "raw_runs.csv") if sub else (n3_dir / "raw_runs.csv")
        if not path.is_file():
            sys.exit(f"missing raw_runs.csv: {path}")
        for r in csv.DictReader(open(path)):
            if r["is_warmup"] == "1" or not r["total_ms"]:
                continue
            key = (r["system"], float(r["s_target"]))
            runs.setdefault(key, []).append(float(r["total_ms"]))
            meta[key] = {
                "s_achieved": r["s_achieved"], "theta": r["theta"],
                "expected_rows": r["expected_rows"],
                "output_rows": r["output_rows"], "rows_match": r["rows_match"],
                "source": sub or "cheap",
            }
    return runs, meta


def load_failures(july_dir: Path):
    """Cells the July sweep observed as OOM/TIMEOUT — n_runs=0, no timing."""
    summary = july_dir / "summary.csv"
    if not summary.is_file():
        sys.exit(f"missing July summary: {summary}")
    date = "unknown"
    meta_path = july_dir / "run_metadata.json"
    if meta_path.is_file():
        import json
        date = json.loads(meta_path.read_text()).get(
            "started_at", "unknown")[:10]
    return ({(r["system"], float(r["s_target"])): r
             for r in csv.DictReader(open(summary)) if r["n_runs"] == "0"},
            date)


def main():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--n3-dir",
                   default=str(PROJECT_DIR / "results" / "e7_selectivity_n3"),
                   help="Directory holding the n=3 invocations "
                        "(default: <project>/results/e7_selectivity_n3)")
    p.add_argument("--july-dir",
                   default=str(PROJECT_DIR / "results" / "e7_selectivity"),
                   help="Original sweep, source of the failing cells "
                        "(default: <project>/results/e7_selectivity)")
    args = p.parse_args()

    n3_dir = Path(args.n3_dir)
    runs, meta = load_measured(n3_dir)
    fails, july_date = load_failures(Path(args.july_dir))

    out = []
    for key in sorted(set(list(runs) + list(fails))):
        system, s = key
        if key in runs:
            t = runs[key]
            mean = statistics.fmean(t)
            out.append({
                "system": system, "s_target": s, "n_runs": len(t),
                "mean_ms": round(mean, 3),
                "median_ms": round(statistics.median(t), 3),
                "min_ms": round(min(t), 3), "max_ms": round(max(t), 3),
                "stddev_ms": (round(statistics.stdev(t), 3)
                              if len(t) > 1 else 0.0),
                "range_pct": round(100.0 * (max(t) - min(t)) / mean, 3),
                **{k: meta[key][k] for k in
                   ("s_achieved", "theta", "expected_rows", "output_rows",
                    "rows_match", "source")},
            })
        else:
            f = fails[key]
            out.append({
                "system": system, "s_target": s, "n_runs": 0,
                "s_achieved": f["s_achieved"], "theta": f["theta"],
                "expected_rows": f["expected_rows"], "mean_ms": "",
                "median_ms": "", "min_ms": "", "max_ms": "", "stddev_ms": "",
                "range_pct": "", "output_rows": f["output_rows"],
                "rows_match": "", "source": f"july_{july_date}",
            })

    out_path = n3_dir / "summary_all.csv"
    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(out)

    measured = sum(1 for r in out if r["n_runs"])
    print(f"{measured} measured cells, {len(out) - measured} asserted failures")
    print(f"summary -> {out_path}")


if __name__ == "__main__":
    main()
