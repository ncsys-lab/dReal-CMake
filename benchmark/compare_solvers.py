#!/usr/bin/env python3
"""Cross-solver comparison over the odeexpr set.

Joins per-solver summary CSVs (benchmark_name, solver_result, cpu_time_s, ...)
by benchmark and reports: solve counts, per-benchmark verdict/timing, SAT↔UNSAT
disagreements, and CPU-time speedups on commonly-solved benchmarks.

Usage:
  compare_solvers.py HEAD=baseline_odeexpr.csv cav26=baseline_odeexpr_cav26.csv [dreal3=baseline_odeexpr_dreal3.csv]
"""
import csv
import statistics
import sys

PAR2_PENALTY = 1200.0  # 2 * 600 s timeout
SOLVED = ("SAT", "UNSAT")


def load(path: str) -> dict[str, dict]:
    out = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            name = row["benchmark_name"].strip().removesuffix(".smt2")
            t = row.get("cpu_time_s", "") or row.get("wall_time_s", "")
            try:
                t = float(t)
            except (ValueError, TypeError):
                t = None
            out[name] = {"result": row["solver_result"].strip(), "cpu": t}
    return out


def par2(entry: dict) -> float:
    return entry["cpu"] if entry["result"] in SOLVED and entry["cpu"] is not None else PAR2_PENALTY


def main():
    solvers: dict[str, dict] = {}
    order: list[str] = []
    for arg in sys.argv[1:]:
        label, path = arg.split("=", 1)
        solvers[label] = load(path)
        order.append(label)

    names = sorted(set().union(*[set(s) for s in solvers.values()]))
    ref = order[0]  # first solver is the reference (HEAD)

    print(f"{'='*78}\nCROSS-SOLVER COMPARISON — {len(names)} benchmarks, reference = {ref}\n{'='*78}\n")

    # Solve counts
    print("Solve counts (within 600 s wall):")
    for lab in order:
        s = solvers[lab]
        sat = sum(1 for n in names if s.get(n, {}).get("result") == "SAT")
        uns = sum(1 for n in names if s.get(n, {}).get("result") == "UNSAT")
        solved = sat + uns
        unsolved = len(names) - solved
        tot_par2 = sum(par2(s[n]) for n in names if n in s)
        print(f"  {lab:8s}  solved {solved:2d}/{len(names)}  (SAT {sat}, UNSAT {uns})  unsolved {unsolved:2d}  "
              f"PAR2-sum {tot_par2:8.0f}s  avg {tot_par2/len(names):6.1f}s")
    print()

    # Verdict disagreements (SAT vs UNSAT between any two solvers — notable)
    disagree = []
    for n in names:
        verdicts = {lab: solvers[lab].get(n, {}).get("result") for lab in order}
        solved_v = {lab: v for lab, v in verdicts.items() if v in SOLVED}
        if len(set(solved_v.values())) > 1:
            disagree.append((n, verdicts))
    if disagree:
        print(f"!! SAT/UNSAT DISAGREEMENTS ({len(disagree)}) — both solved but differ (delta-completeness or bug):")
        for n, v in disagree:
            print(f"   {n}")
            print(f"      " + "  ".join(f"{lab}={v[lab]}" for lab in order))
        print()
    else:
        print("No SAT/UNSAT disagreements among solved benchmarks.\n")

    # Solve-set deltas vs reference
    for lab in order[1:]:
        only_ref = [n for n in names
                    if solvers[ref].get(n, {}).get("result") in SOLVED
                    and solvers[lab].get(n, {}).get("result") not in SOLVED]
        only_lab = [n for n in names
                    if solvers[lab].get(n, {}).get("result") in SOLVED
                    and solvers[ref].get(n, {}).get("result") not in SOLVED]
        print(f"Solve-set {ref} vs {lab}:")
        print(f"  solved by {ref} but not {lab} ({len(only_ref)}): " + (", ".join(only_ref) or "—"))
        print(f"  solved by {lab} but not {ref} ({len(only_lab)}): " + (", ".join(only_lab) or "—"))
        print()

    # Speedup on commonly-solved (CPU time)
    for lab in order[1:]:
        common = [n for n in names
                  if solvers[ref].get(n, {}).get("result") in SOLVED
                  and solvers[lab].get(n, {}).get("result") in SOLVED
                  and solvers[ref][n]["cpu"] and solvers[lab][n]["cpu"]]
        if common:
            ratios = [solvers[lab][n]["cpu"] / solvers[ref][n]["cpu"] for n in common]
            ref_tot = sum(solvers[ref][n]["cpu"] for n in common)
            lab_tot = sum(solvers[lab][n]["cpu"] for n in common)
            print(f"CPU time on {len(common)} commonly-solved ({lab} / {ref}):")
            print(f"  total: {ref}={ref_tot:.2f}s  {lab}={lab_tot:.2f}s  "
                  f"aggregate ratio {lab_tot/ref_tot:.2f}x")
            print(f"  per-benchmark ratio: median {statistics.median(ratios):.2f}x  "
                  f"min {min(ratios):.2f}x  max {max(ratios):.2f}x  "
                  f"({sum(1 for r in ratios if r>1)} slower / {sum(1 for r in ratios if r<1)} faster than {ref})")
            print()

    # Per-benchmark table
    print(f"{'-'*78}\nPER-BENCHMARK (result / CPU s):\n{'-'*78}")
    hdr = f"{'benchmark':44s}" + "".join(f"{lab:>16s}" for lab in order)
    print(hdr)
    for n in names:
        cells = ""
        for lab in order:
            e = solvers[lab].get(n)
            if e is None:
                cells += f"{'—':>16s}"
            else:
                t = f"{e['cpu']:.2f}" if e["cpu"] is not None else "—"
                cells += f"{e['result']+' '+t:>16s}"
        print(f"{n:44s}{cells}")


if __name__ == "__main__":
    main()
