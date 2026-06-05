#!/usr/bin/env python3
"""Compare a benchmark run against baseline, flag regressions and exceptional results.

Usage: python3 aggregate.py <results_dir>
  - Reads <results_dir>/summary.csv
  - Reads benchmark/baseline.csv and benchmark/state.json
  - Writes <results_dir>/anomaly_report.txt
  - Updates benchmark/state.json
  - Prints JSON summary to stdout (for Haiku subagent consumption)
"""
import csv
import json
import os
import sys
from datetime import datetime, timezone

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

REGRESSION_RATIO = 1.5   # wall time > 1.5x baseline → regression
EXCEPTIONAL_RATIO = 0.6  # wall time < 0.6x baseline → exceptional


def load_baseline(baseline_csv: str, column: str = "DRPM_0L") -> dict[str, dict]:
    """Returns {benchmark_name: {time_s: float, result: str}}"""
    with open(baseline_csv) as f:
        rows = list(csv.reader(f))

    # rows[0]: group headers (elapsed_time x3, solver_result x3)
    # rows[1]: sub-headers (DRPM_0L, DRPM_16L_200ms, dReal3, ...)
    # rows[2]: index label row
    # rows[3+]: data

    subheaders = rows[1]
    # Find column indices for the chosen variant
    time_col = None
    result_col = None
    for i, h in enumerate(subheaders):
        if h.strip() == column:
            if time_col is None:
                time_col = i
            else:
                result_col = i
                break

    if time_col is None or result_col is None:
        raise ValueError(f"Column '{column}' not found in baseline CSV (found: {subheaders})")

    baseline = {}
    for row in rows[3:]:
        if not row or not row[0].strip():
            continue
        # Strip .smt2 so keys match result filenames (which use csv_name minus .smt2)
        name = row[0].strip().removesuffix(".smt2")
        try:
            time_s = float(row[time_col])
        except (ValueError, IndexError):
            time_s = None
        result_raw = row[result_col].strip() if result_col < len(row) else ""
        # Normalize: SolverResult.SAT → SAT, SolverResult.UNS → UNSAT, etc.
        result = result_raw.replace("SolverResult.", "")
        if result == "UNS":
            result = "UNSAT"
        elif result in ("TIM", "MEM", "UNK"):
            pass  # keep as-is
        baseline[name] = {"time_s": time_s, "result": result}
    return baseline


def load_summary(summary_csv: str) -> list[dict]:
    with open(summary_csv) as f:
        return list(csv.DictReader(f))


def main():
    if len(sys.argv) < 2:
        print("Usage: aggregate.py <results_dir>", file=sys.stderr)
        sys.exit(1)

    results_dir = sys.argv[1]
    state_path = os.path.join(SCRIPT_DIR, "state.json")
    summary_csv = os.path.join(results_dir, "summary.csv")

    with open(state_path) as f:
        state = json.load(f)

    baseline_column = state.get("baseline_column", "DRPM_0L")
    baseline_file = os.path.join(SCRIPT_DIR, "..", state.get("baseline_source", "benchmark/baseline.csv"))
    baseline_file = os.path.normpath(baseline_file)

    baseline = load_baseline(baseline_file, baseline_column)
    summary = load_summary(summary_csv)

    regressions = []       # {name, reason, baseline_time, current_time, baseline_result, current_result}
    exceptional_list = []  # {name, reason, baseline_time, current_time}
    resolved_anomalies = []

    prev_anomalies = set(state.get("anomalies", []))
    prev_exceptional = set(state.get("exceptional", []))

    new_anomalies = set(prev_anomalies)
    new_exceptional = set(prev_exceptional)

    for row in summary:
        name = row["benchmark_name"]
        cur_result = row["solver_result"]
        cur_time = float(row["wall_time_s"]) if row["wall_time_s"] else None

        base = baseline.get(name)
        if base is None:
            continue  # not in baseline, skip

        base_time = base["time_s"]
        base_result = base["result"]
        base_timed_out = base_result in ("TIM", "MEM", "UNK") or (base_time is not None and base_time >= 180)

        # --- Correctness regression (always HIGH PRIORITY) ---
        result_flip = False
        if cur_result in ("SAT", "UNSAT") and base_result in ("SAT", "UNSAT") and cur_result != base_result:
            regressions.append({
                "name": name,
                "priority": "CORRECTNESS",
                "reason": f"Result changed: baseline={base_result} current={cur_result}",
                "baseline_time": base_time,
                "current_time": cur_time,
                "baseline_result": base_result,
                "current_result": cur_result,
            })
            new_anomalies.add(name)
            result_flip = True

        # --- Solve→timeout/OOM regression ---
        if not result_flip and base_result in ("SAT", "UNSAT") and cur_result in ("TIM", "OOM", "ERR"):
            regressions.append({
                "name": name,
                "priority": "HIGH",
                "reason": f"Baseline solved ({base_result}) but current {cur_result}",
                "baseline_time": base_time,
                "current_time": cur_time,
                "baseline_result": base_result,
                "current_result": cur_result,
            })
            new_anomalies.add(name)

        # --- Timeout→solve improvement ---
        elif base_timed_out and cur_result in ("SAT", "UNSAT"):
            exceptional_list.append({
                "name": name,
                "reason": f"Baseline {base_result} but current solved as {cur_result} in {cur_time:.1f}s",
                "baseline_time": base_time,
                "current_time": cur_time,
            })
            new_exceptional.add(name)
            if name in new_anomalies:
                new_anomalies.discard(name)

        # --- Timing regression (only when baseline also solved) ---
        elif (not result_flip and not base_timed_out and
              cur_time is not None and base_time is not None and
              cur_result in ("SAT", "UNSAT") and
              cur_time > REGRESSION_RATIO * base_time):
            regressions.append({
                "name": name,
                "priority": "TIMING",
                "reason": f"{cur_time:.1f}s vs baseline {base_time:.1f}s ({cur_time/base_time:.2f}x)",
                "baseline_time": base_time,
                "current_time": cur_time,
                "baseline_result": base_result,
                "current_result": cur_result,
            })
            new_anomalies.add(name)

        # --- Exceptional speedup ---
        elif (not base_timed_out and
              cur_time is not None and base_time is not None and
              cur_result in ("SAT", "UNSAT") and
              cur_time < EXCEPTIONAL_RATIO * base_time):
            exceptional_list.append({
                "name": name,
                "reason": f"{cur_time:.1f}s vs baseline {base_time:.1f}s ({cur_time/base_time:.2f}x)",
                "baseline_time": base_time,
                "current_time": cur_time,
            })
            new_exceptional.add(name)

        # --- Resolved anomaly ---
        if name in prev_anomalies and name not in {r["name"] for r in regressions}:
            resolved_anomalies.append(name)
            new_anomalies.discard(name)

    # Write anomaly report
    report_path = os.path.join(results_dir, "anomaly_report.txt")
    with open(report_path, "w") as f:
        f.write(f"Benchmark Run: {results_dir}\n")
        f.write(f"Ran: {len(summary)} benchmarks\n\n")

        if regressions:
            f.write(f"=== REGRESSIONS ({len(regressions)}) ===\n")
            for r in sorted(regressions, key=lambda x: x["priority"]):
                f.write(f"  [{r['priority']}] {r['name']}\n")
                f.write(f"    {r['reason']}\n")
        else:
            f.write("=== NO REGRESSIONS ===\n")

        f.write("\n")
        if exceptional_list:
            f.write(f"=== EXCEPTIONAL ({len(exceptional_list)}) ===\n")
            for e in exceptional_list:
                f.write(f"  {e['name']}\n")
                f.write(f"    {e['reason']}\n")

        if resolved_anomalies:
            f.write(f"\n=== RESOLVED ANOMALIES ({len(resolved_anomalies)}) ===\n")
            for name in resolved_anomalies:
                f.write(f"  {name}\n")

    # Update state.json
    state["anomalies"] = sorted(new_anomalies)
    state["exceptional"] = sorted(new_exceptional)
    state["last_run"] = datetime.now(timezone.utc).isoformat()
    state["last_run_dir"] = results_dir
    state.setdefault("runs", []).append({
        "timestamp": state["last_run"],
        "results_dir": results_dir,
        "n_ran": len(summary),
        "n_regressions": len(regressions),
        "n_exceptional": len(exceptional_list),
        "n_resolved": len(resolved_anomalies),
        "correctness_flips": [r["name"] for r in regressions if r["priority"] == "CORRECTNESS"],
    })

    with open(state_path, "w") as f:
        json.dump(state, f, indent=2)

    # JSON summary to stdout (consumed by Haiku subagent)
    summary_json = {
        "n_ran": len(summary),
        "n_regressions": len(regressions),
        "n_exceptional": len(exceptional_list),
        "n_resolved": len(resolved_anomalies),
        "correctness_flips": [r["name"] for r in regressions if r["priority"] == "CORRECTNESS"],
        "regressions": regressions,
        "exceptional": exceptional_list,
        "resolved": resolved_anomalies,
        "anomaly_report": open(report_path).read(),
    }
    print(json.dumps(summary_json, indent=2))


if __name__ == "__main__":
    main()
