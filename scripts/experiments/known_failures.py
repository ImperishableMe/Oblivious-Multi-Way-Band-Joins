#!/usr/bin/env python3
"""
Known-failure inventory shared by the experiment runners.

A cell that has already been observed to OOM, exceed its budget, or sit behind
a strictly larger failure carries no timing to measure — re-running it only
burns wall-clock. At n=3 the cost is not marginal: E3's
`obliviator_chained @ hi_large` alone is an hour per attempt, and the Full MWJ
OOM crawls climb to hundreds of GB before the kernel kills them.

The runners therefore accept `--known-failures <csv>`: every listed cell is
recorded with its previously observed outcome and never executed. Rows land in
raw_runs.csv with an empty `total_ms`, so each runner's `write_summary_csv`
emits its usual `n_runs=0` sentinel and the plot scripts render the cell
exactly as they render a failure measured in that same sweep.

CSV columns (system, cell, outcome are required; the rest are provenance):

  system         runner system key — nebuladb, full_mwj_no_filter,
                 obliviator_chained
  cell           experiment-specific cell id: E3 the dataset label, E5 the
                 density variant, E4 the query name
  outcome        OOM | TIMEOUT | SKIPPED | UNSUPPORTED
  date_observed  ISO date the outcome was measured
  commit         git commit the outcome was measured at
  note           free text — why it failed, which budget applied

The canonical inventories live in results/known_failures/.
"""

import csv
import sys
from pathlib import Path

VALID_OUTCOMES = ("OOM", "TIMEOUT", "SKIPPED", "UNSUPPORTED")

FIELDS = ["system", "cell", "outcome", "date_observed", "commit", "note"]


def load(path) -> dict:
    """Read a known-failure CSV into {(system, cell): row}."""
    path = Path(path)
    if not path.is_file():
        sys.exit(f"--known-failures file not found: {path}")
    known = {}
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        header = reader.fieldnames or []
        missing = [c for c in ("system", "cell", "outcome") if c not in header]
        if missing:
            sys.exit(f"{path}: missing required column(s): {missing}")
        for lineno, row in enumerate(reader, start=2):
            system = (row.get("system") or "").strip()
            cell = (row.get("cell") or "").strip()
            outcome = (row.get("outcome") or "").strip().upper()
            if not system and not cell:
                continue  # blank padding line
            if not system or not cell:
                sys.exit(f"{path}:{lineno}: system and cell must both be set")
            if outcome not in VALID_OUTCOMES:
                sys.exit(f"{path}:{lineno}: outcome {outcome!r} is not one of "
                         f"{list(VALID_OUTCOMES)}")
            key = (system, cell)
            if key in known:
                sys.exit(f"{path}:{lineno}: duplicate entry for {system} @ {cell}")
            known[key] = {**row, "system": system, "cell": cell,
                          "outcome": outcome}
    return known


def validate(known, all_systems, all_cells, path) -> None:
    """Fail fast on entries naming a system or cell the experiment has no
    concept of — a typo here would silently execute a cell we meant to skip
    (or worse, skip one we meant to run). Validation is against the
    experiment's full universe, not the current subset, so narrowing a sweep
    with --systems / --datasets stays legal."""
    all_systems, all_cells = set(all_systems), set(all_cells)
    bad = sorted(f"{s} @ {c}" for (s, c) in known
                 if s not in all_systems or c not in all_cells)
    if bad:
        sys.exit(f"{path}: entries name no cell of this experiment: {bad}\n"
                 f"  known systems: {sorted(all_systems)}\n"
                 f"  known cells  : {sorted(all_cells)}")


def outcome_for(known, system, cell):
    """The recorded outcome for a cell, or None if it should be executed."""
    entry = known.get((system, cell))
    return entry["outcome"] if entry else None


def announce(known, path, systems=None, cells=None) -> None:
    """Print the inventory that applies to this sweep, so the log says plainly
    which cells were asserted rather than measured."""
    if not known:
        return
    applies = {k: v for k, v in known.items()
               if (systems is None or k[0] in set(systems))
               and (cells is None or k[1] in set(cells))}
    print(f"known failures ({path}): {len(applies)} of {len(known)} entries "
          f"apply to this sweep — recorded, never executed")
    for (system, cell), row in sorted(applies.items()):
        seen = (row.get("date_observed") or "?").strip()
        commit = (row.get("commit") or "").strip()[:9] or "?"
        print(f"  {system:20s} @ {cell:14s} -> {row['outcome']:11s} "
              f"(observed {seen}, {commit})")
    print()
