#!/usr/bin/env python3
"""
Plot results of the one-hop thread scaling experiment.

Reads results/one_hop_thread_scaling/headline.csv (produced by
run_one_hop_thread_scaling.py) and writes:

  results/one_hop_thread_scaling/thread_scaling.png  -- two-panel figure:
    (left)  latency vs threads, log-log, one line per system
    (right) self-speedup vs threads (relative to t=1), with ideal y=x line

Usage:
  python3 scripts/experiments/plot_one_hop_thread_scaling.py
  python3 scripts/experiments/plot_one_hop_thread_scaling.py --headline <path>
"""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt

PROJECT_DIR = Path(__file__).resolve().parents[2]
DEFAULT_HEADLINE = (
    PROJECT_DIR / "results" / "one_hop_thread_scaling" / "headline.csv"
)


def load_headline(path: Path):
    threads, ours, obliv = [], [], []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            t = int(r["threads"])
            o = float(r["ours_online_ms"]) if r["ours_online_ms"] else None
            b = float(r["obliviator_oblivious_work_ms"]) if r["obliviator_oblivious_work_ms"] else None
            if o is None or b is None:
                continue
            threads.append(t)
            ours.append(o)
            obliv.append(b)
    order = sorted(range(len(threads)), key=lambda i: threads[i])
    threads = [threads[i] for i in order]
    ours = [ours[i] for i in order]
    obliv = [obliv[i] for i in order]
    return threads, ours, obliv


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--headline", type=Path, default=DEFAULT_HEADLINE)
    ap.add_argument("--out", type=Path, default=None,
                    help="output PNG (default: alongside headline.csv)")
    args = ap.parse_args()

    if not args.headline.exists():
        raise SystemExit(f"missing: {args.headline}")
    out_path = args.out or args.headline.parent / "thread_scaling.png"

    threads, ours_ms, obliv_ms = load_headline(args.headline)
    if not threads:
        raise SystemExit("no rows in headline.csv")

    ours_speedup = [ours_ms[0] / v for v in ours_ms]
    obliv_speedup = [obliv_ms[0] / v for v in obliv_ms]

    fig, (ax_lat, ax_sp) = plt.subplots(1, 2, figsize=(11, 4.4))

    # --- left: latency vs threads ---
    ax_lat.plot(threads, ours_ms,  marker="o", label="Ours (ONLINE)",
                color="tab:blue")
    ax_lat.plot(threads, obliv_ms, marker="s", label="Obliviator (OBLIVIOUS WORK)",
                color="tab:orange")
    ax_lat.set_xscale("log", base=2)
    ax_lat.set_yscale("log")
    ax_lat.set_xticks(threads)
    ax_lat.set_xticklabels([str(t) for t in threads])
    ax_lat.set_xlabel("Threads")
    ax_lat.set_ylabel("Latency (ms)")
    ax_lat.set_title("One-hop latency vs threads (Banking 1M)")
    ax_lat.grid(True, which="both", alpha=0.3)
    ax_lat.legend()

    # annotate raw ms next to each point on the lower line of the pair
    for t, y in zip(threads, ours_ms):
        ax_lat.annotate(f"{y:.0f}", (t, y), textcoords="offset points",
                        xytext=(4, -10), fontsize=7, color="tab:blue")
    for t, y in zip(threads, obliv_ms):
        ax_lat.annotate(f"{y:.0f}", (t, y), textcoords="offset points",
                        xytext=(4, 6), fontsize=7, color="tab:orange")

    # --- right: self-speedup ---
    ax_sp.plot(threads, ours_speedup,  marker="o", label="Ours",
               color="tab:blue")
    ax_sp.plot(threads, obliv_speedup, marker="s", label="Obliviator",
               color="tab:orange")
    # ideal linear y=x line (clipped to thread range)
    ideal = list(threads)
    ax_sp.plot(ideal, ideal, linestyle="--", color="grey",
               alpha=0.6, label="Ideal (linear)")
    ax_sp.set_xscale("log", base=2)
    ax_sp.set_yscale("log", base=2)
    ax_sp.set_xticks(threads)
    ax_sp.set_xticklabels([str(t) for t in threads])
    ax_sp.set_yticks([1, 2, 4, 8, 16, 32, 64, 120])
    ax_sp.set_yticklabels([str(v) for v in [1, 2, 4, 8, 16, 32, 64, 120]])
    ax_sp.set_xlabel("Threads")
    ax_sp.set_ylabel("Self-speedup vs t=1")
    ax_sp.set_title("Parallel scaling")
    ax_sp.grid(True, which="both", alpha=0.3)
    ax_sp.legend()

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"[done] wrote {out_path}")


if __name__ == "__main__":
    main()
