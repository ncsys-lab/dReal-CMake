#!/usr/bin/env python3
"""Compare a probe run against the frozen probe baseline (self-contained).

The ODE-heavy probe set (probe_odes.tsv) is NOT in baseline_local.csv, so the
gate's aggregate.py/state.json machinery can't score it. This comparator is
self-contained: it diffs a probe run's summary.csv against probe_baseline.csv,
reports per-benchmark PAR2 ratios, the net PAR2 ratio, regressions (>1.5x),
exceptional speedups (<0.6x), and — critically — any SAT/UNSAT correctness
flips (vs the baseline result, and vs ground_truth from baseline.csv where
available). Reuses par2_time + thresholds from aggregate.py so scoring matches
the gate exactly.

Usage: python3 probe_compare.py <run_summary.csv> [--baseline probe_baseline.csv]
"""
import argparse
import csv
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
from aggregate import par2_time, REGRESSION_RATIO, EXCEPTIONAL_RATIO  # noqa: E402


def load_summary(path: str) -> dict:
    out = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            name = row["benchmark_name"].removesuffix(".smt2")
            try:
                t = float(row["wall_time_s"])
            except (ValueError, KeyError, TypeError):
                t = None
            out[name] = {"result": row["solver_result"], "time": t}
    return out


def load_ground_truth(frozen_csv: str) -> dict:
    """benchmark name (no .smt2) -> ground_truth string (SAT/UNSAT/'')."""
    gt = {}
    if not os.path.exists(frozen_csv):
        return gt
    with open(frozen_csv) as f:
        rows = list(csv.reader(f))
    for row in rows[3:]:
        if row and row[0].strip():
            name = row[0].strip().removesuffix(".smt2")
            gt[name] = (row[7].strip() if len(row) > 7 else "")
    return gt


SOLVED = ("SAT", "UNSAT")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_summary")
    ap.add_argument("--baseline", default=os.path.join(SCRIPT_DIR, "probe_baseline.csv"))
    ap.add_argument("--frozen", default=os.path.join(SCRIPT_DIR, "baseline.csv"))
    args = ap.parse_args()

    base = load_summary(args.baseline)
    run = load_summary(args.run_summary)
    gt = load_ground_truth(args.frozen)

    rows = []
    flips = []          # (name, base_result, run_result, kind)
    regressions = []    # (name, ratio)
    exceptional = []    # (name, ratio)
    sum_base = sum_run = 0.0

    for name, b in sorted(base.items()):
        r = run.get(name)
        if r is None:
            rows.append((name, b["result"], "MISSING", b["time"], None, None))
            continue
        pb = par2_time(b["result"], b["time"])
        pr = par2_time(r["result"], r["time"])
        sum_base += pb
        sum_run += pr
        ratio = pr / pb if pb else None

        # correctness flip: SAT<->UNSAT between baseline and run, or a solved
        # result that now contradicts ground_truth.
        kind = None
        if b["result"] in SOLVED and r["result"] in SOLVED and b["result"] != r["result"]:
            kind = f"BASELINE-FLIP {b['result']}->{r['result']}"
        g = gt.get(name, "")
        if g in SOLVED and r["result"] in SOLVED and r["result"] != g:
            kind = (kind + "; " if kind else "") + f"CONTRADICTS-GT (gt={g})"
        if kind:
            flips.append((name, b["result"], r["result"], kind))

        if ratio is not None and r["result"] in SOLVED and b["result"] in SOLVED:
            if ratio > REGRESSION_RATIO:
                regressions.append((name, ratio))
            elif ratio < EXCEPTIONAL_RATIO:
                exceptional.append((name, ratio))
        rows.append((name, b["result"], r["result"], b["time"], r["time"], ratio))

    net = sum_run / sum_base if sum_base else None

    print(f"{'benchmark':62s} {'base':6s} {'run':6s} {'base_s':>8s} {'run_s':>8s} {'ratio':>6s}")
    for name, br, rr, bt, rt, ratio in rows:
        bt_s = f"{bt:8.2f}" if bt is not None else "     n/a"
        rt_s = f"{rt:8.2f}" if rt is not None else "     n/a"
        rr_s = f"{ratio:6.2f}" if ratio is not None else "   n/a"
        flag = ""
        if ratio is not None and rr in SOLVED and br in SOLVED:
            if ratio > REGRESSION_RATIO: flag = " REGRESSION"
            elif ratio < EXCEPTIONAL_RATIO: flag = " exceptional"
        if br != rr: flag += " <RESULT-CHANGE>"
        print(f"{name[:62]:62s} {br:6s} {rr:6s} {bt_s} {rt_s} {rr_s}{flag}")

    print("\n" + "=" * 72)
    if flips:
        print("*** CORRECTNESS FLIPS (HALT) ***")
        for name, br, rr, kind in flips:
            print(f"  {name}: {kind}")
    else:
        print("No correctness flips.")
    print(f"net PAR2 ratio (run/base): {net:.3f}" if net else "net PAR2: n/a")
    if net is not None:
        pct = (1 - net) * 100
        verb = "FASTER" if pct >= 0 else "SLOWER"
        print(f"  -> {abs(pct):.1f}% {verb} overall")
    print(f"regressions (>1.5x): {len(regressions)}  {[f'{n}:{r:.2f}' for n,r in regressions]}")
    print(f"exceptional (<0.6x): {len(exceptional)}  {[f'{n}:{r:.2f}' for n,r in exceptional]}")

    # exit non-zero on any correctness flip so callers can gate on it.
    sys.exit(2 if flips else 0)


if __name__ == "__main__":
    main()
