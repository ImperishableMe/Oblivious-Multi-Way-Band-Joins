#!/usr/bin/env python3
"""
Plot the E2 NebulaDB AML scaling experiment.

Reads results/e2_scaling/summary.csv (produced by run_e2_scaling.py) and
writes results/e2_scaling/e2_scaling.png -- a grouped bar chart of NebulaDB
end-to-end latency, grouped by chain length (hops), one bar per dataset
(HI-Small, HI-Medium). Linear y-axis, seconds, with value labels.

Only completed cells are plotted (rows with a numeric median_total_ms); OOM /
SKIPPED cells (e.g. HI-Large) are dropped automatically.

Usage:
  python3 scripts/experiments/plot_e2_scaling.py
  python3 scripts/experiments/plot_e2_scaling.py --summary <path> --out <png>
"""

import argparse
import csv
import re
from pathlib import Path

import matplotlib.pyplot as plt

PROJECT_DIR = Path(__file__).resolve().parents[2]
DEFAULT_SUMMARY = PROJECT_DIR / "results" / "e2_scaling" / "summary.csv"

# datasets to include, in scaling order, with a short display label + color
DATASETS = [
    ("ibm_aml_hi_small", "HI-Small (5.1M edges)", "tab:blue"),
    ("ibm_aml_hi_medium", "HI-Medium (31.9M edges)", "tab:orange"),
]


def hop_of(query):
    m = re.search(r"(\d+)hop", query)
    return int(m.group(1)) if m else None


def load_summary(path):
    """Return {dataset: {hop: total_s}} for completed nebuladb cells."""
    out = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if r["system"] != "nebuladb":
                continue
            val = r.get("median_total_ms", "").strip()
            if not val:  # OOM / SKIPPED cells have no time
                continue
            try:
                total_s = float(val) / 1000.0
            except ValueError:
                continue
            out.setdefault(r["dataset"], {})[hop_of(r["query"])] = total_s
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    ap.add_argument("--out", type=Path, default=None,
                    help="output PNG (default: alongside summary.csv)")
    args = ap.parse_args()

    if not args.summary.exists():
        raise SystemExit(f"missing: {args.summary}")
    out_path = args.out or args.summary.parent / "e2_scaling.png"

    data = load_summary(args.summary)
    if not data:
        raise SystemExit("no completed nebuladb rows in summary.csv")

    # hops present across the included datasets, sorted
    hops = sorted({h for dkey, _, _ in DATASETS for h in data.get(dkey, {})})
    if not hops:
        raise SystemExit("no hops found for the configured datasets")

    n_sets = len(DATASETS)
    bar_w = 0.8 / n_sets
    x = list(range(len(hops)))

    fig, ax = plt.subplots(figsize=(8.5, 5.0))

    for i, (dkey, dlabel, color) in enumerate(DATASETS):
        secs = [data.get(dkey, {}).get(h) for h in hops]
        offsets = [xi + (i - (n_sets - 1) / 2) * bar_w for xi in x]
        # matplotlib skips None heights; pair only the present ones for labels
        bars = ax.bar(offsets, [s if s is not None else 0 for s in secs],
                      width=bar_w, label=dlabel, color=color)
        for rect, s in zip(bars, secs):
            if s is None:
                continue
            ax.annotate(f"{s:.0f}", (rect.get_x() + rect.get_width() / 2,
                                     rect.get_height()),
                        textcoords="offset points", xytext=(0, 3),
                        ha="center", fontsize=8, color=color)

    ax.set_xticks(x)
    ax.set_xticklabels([f"{h}-hop" for h in hops])
    ax.set_xlabel("Chain length")
    ax.set_ylabel("End-to-end latency (s)")
    ax.set_title("NebulaDB scaling on IBM AML (W4): latency by chain length")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"[done] wrote {out_path}")


if __name__ == "__main__":
    main()
