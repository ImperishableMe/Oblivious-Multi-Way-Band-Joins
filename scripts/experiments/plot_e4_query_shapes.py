#!/usr/bin/env python3
"""
Plot an E4 query-shape sweep (system comparison, one dataset).

Reads results/e4_query_shapes/summary.csv (produced by run_e4_query_shapes.py)
and writes a grouped bar chart of end-to-end latency by query shape — the
chain context points (2-hop, 3-hop) followed by the size-matched 4-edge
quartet (4-hop, fan-in, fan-out, tree) — one bar per system. Linear y-axis,
seconds, with value labels.

Failed/skipped cells (OOM, TIMEOUT, SKIPPED, UNSUPPORTED) keep their bar slot
and are marked with the sentinel, so a baseline that cannot run a shape reads
as a finding rather than missing data.

Colors and display names follow the E1 figure set: same system -> same color
across the paper (Tableau-10 subset, CVD-safe blue/orange pair + purple).
nebuladb is labeled "Graphite" per the canonical naming.

Usage:
  python3 scripts/experiments/plot_e4_query_shapes.py
  python3 scripts/experiments/plot_e4_query_shapes.py --summary <path> --out <png>
"""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt

PROJECT_DIR = Path(__file__).resolve().parents[2]
E4_DIR = PROJECT_DIR / "results" / "e4_query_shapes"

# System -> (display label, color). Fixed order = draw/legend order; color
# follows the system, never its rank — identical to the E1 figure set.
SYSTEMS = [
    ("nebuladb", "Graphite (one-hop + filtered MWJ)", "#4E79A7"),
    ("obliviator_chained", "Obliviator multiway", "#B07AA1"),
    ("full_mwj_no_filter", "Full MWJ", "#F28E2B"),
]

# x-axis order and display names: chains by depth, then the 4-edge shapes.
QUERY_ORDER = [
    ("aml_2hop", "2-hop"),
    ("aml_3hop", "3-hop"),
    ("aml_4hop", "4-hop"),
    ("aml_fanin", "fan-in"),
    ("aml_fanout", "fan-out"),
    ("aml_tree", "tree"),
]

# Sentinels that appear in output_rows for a failed/skipped cell.
FAIL_TOKENS = {"OOM", "TIMEOUT", "SKIPPED", "UNSUPPORTED"}


def load_summary(path):
    """Return ({system: {query: seconds}}, {system: {query: fail_token}})."""
    times, status = {}, {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            sysname = r["system"]
            query = r["query"]
            val = (r.get("median_ms") or "").strip()
            rows = (r.get("output_rows") or "").strip()
            if val:
                try:
                    times.setdefault(sysname, {})[query] = float(val) / 1000.0
                    continue
                except ValueError:
                    pass
            if rows in FAIL_TOKENS:
                status.setdefault(sysname, {})[query] = rows
    return times, status


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--summary", type=Path, default=E4_DIR / "summary.csv",
                    help=f"summary.csv path (default: {E4_DIR / 'summary.csv'})")
    ap.add_argument("--out", type=Path, default=None,
                    help="output PNG (default: alongside summary.csv, "
                         "e4_query_shapes.png)")
    ap.add_argument("--title", default=None, help="override the chart title")
    args = ap.parse_args()

    if not args.summary.exists():
        raise SystemExit(f"missing: {args.summary}")
    out_path = args.out or args.summary.parent / "e4_query_shapes.png"

    times, status = load_summary(args.summary)
    if not times and not status:
        raise SystemExit("no cells in summary.csv")

    def cells(s):
        return ({q for q, _ in QUERY_ORDER if q in times.get(s, {})}
                | {q for q, _ in QUERY_ORDER if q in status.get(s, {})})

    # A system gets a slot if it has ANY cell (numeric or failed), so a
    # baseline that failed everywhere still shows a column of sentinel marks.
    drawn = [(s, lbl, c) for (s, lbl, c) in SYSTEMS if cells(s)]
    plotted = [(q, lbl) for q, lbl in QUERY_ORDER
               if any(q in cells(s) for (s, _, _) in drawn)]
    if not drawn or not plotted:
        raise SystemExit("no cells to plot")

    n_sets = len(drawn)
    bar_w = 0.8 / max(n_sets, 1)
    x = list(range(len(plotted)))

    fig, ax = plt.subplots(figsize=(9.5, 5.2))

    ymax = max((times.get(s, {}).get(q, 0.0)
                for (s, _, _) in drawn for q, _ in plotted), default=1.0)

    for i, (sysname, label, color) in enumerate(drawn):
        offsets = [xi + (i - (n_sets - 1) / 2) * bar_w for xi in x]
        heights = [times.get(sysname, {}).get(q) for q, _ in plotted]
        ax.bar(offsets, [h if h is not None else 0.0 for h in heights],
               width=bar_w, label=label, color=color, zorder=3)
        for xpos, h, (q, _) in zip(offsets, heights, plotted):
            if h is not None:
                ax.annotate(f"{h:.0f}", (xpos, h), textcoords="offset points",
                            xytext=(0, 3), ha="center", fontsize=8, color=color)
            else:
                tok = status.get(sysname, {}).get(q)
                if tok:  # OOM / TIMEOUT / SKIPPED / UNSUPPORTED
                    ax.annotate(tok, (xpos, 0.0), textcoords="offset points",
                                xytext=(0, 4), ha="center", va="bottom",
                                fontsize=7.5, color=color, rotation=90,
                                fontweight="bold")

    ax.set_xticks(x)
    ax.set_xticklabels([lbl for _, lbl in plotted])
    ax.set_xlabel("Query shape (4-hop, fan-in, fan-out, tree are size-matched: 4 edges)")
    ax.set_ylabel("End-to-end latency (s)")
    ax.set_ylim(0, ymax * 1.15)
    title = args.title or "E4: system comparison by query shape (ibm_aml_hi_small)"
    ax.set_title(title)
    ax.grid(True, axis="y", alpha=0.3, zorder=0)
    ax.legend()

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"[done] wrote {out_path}")


if __name__ == "__main__":
    main()
