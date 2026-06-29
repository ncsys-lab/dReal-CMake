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

from odeexpr import family_of, FAMILY_WEIGHTS, MANIFEST_FAMILY_NAMES

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

REGRESSION_RATIO = 1.5   # PAR2 time > 1.5x baseline → regression
EXCEPTIONAL_RATIO = 0.6  # PAR2 time < 0.6x baseline → exceptional

SOLVER_TIMEOUT_S = 600   # must match run_batch.sh `timeout 600`
PAR2_PENALTY_S = 2 * SOLVER_TIMEOUT_S  # 1200 s

# Timing resolution / noise floors. gtime reports CPU time to ~0.01 s, so ratios
# below that are meaningless. NEGLIGIBLE_S: when BOTH baseline and current are
# this fast, skip the ratio check entirely (sub-0.1 s jitter on a multi-tenant
# box is noise, not a regression). TIME_FLOOR_S: clamp the divisor so a genuine
# instant→seconds jump still divides (and flags) instead of hitting 0.
NEGLIGIBLE_S = 0.1
TIME_FLOOR_S = 0.01


def safe_ratio(cur: float, base: float) -> float:
    """PAR2 ratio with both operands floored at the measurement resolution."""
    return max(cur, TIME_FLOOR_S) / max(base, TIME_FLOOR_S)

# Each manifest family (odeexpr_v1/v2) keeps its own local baseline CSV,
# baseline_<family>.csv, produced by `do_baseline_odeexpr.sh <family>` (the flat
# families live in baseline.csv / baseline_local.csv).
MANIFEST_BASELINES = {fam: os.path.join(SCRIPT_DIR, f"baseline_{fam}.csv")
                      for fam in MANIFEST_FAMILY_NAMES}


def row_time(row: dict) -> float | None:
    """Primary timing for a result/baseline row: CPU time (cpu_time_s) if
    present, else wall_time_s (older baselines predate the cpu_time_s column)."""
    for col in ("cpu_time_s", "wall_time_s"):
        val = row.get(col, "")
        if val not in (None, ""):
            try:
                return float(val)
            except (ValueError, TypeError):
                pass
    return None


def par2_time(result: str, wall_time_s: float | None) -> float:
    """PAR2-penalized time: actual time if solved, 2x timeout (600 s) otherwise."""
    if result in ("SAT", "UNSAT") and wall_time_s is not None:
        return wall_time_s
    return PAR2_PENALTY_S


def load_baseline(baseline_csv: str, column: str = "DRPM_0L") -> dict[str, dict]:
    """Returns {benchmark_name: {time_s: float, result: str}}"""
    with open(baseline_csv) as f:
        reader = csv.DictReader(f)
        rows_as_dicts = list(reader)

    # If we have benchmark_name, wall_time_s, solver_result columns, parse as new format
    if reader.fieldnames and "benchmark_name" in reader.fieldnames:
        baseline = {}
        for row in rows_as_dicts:
            if not row or not row.get("benchmark_name", "").strip():
                continue
            name = row["benchmark_name"].strip().removesuffix(".smt2")
            time_s = row_time(row)
            result = row.get("solver_result", "").strip()
            ground_truth = row.get("ground_truth", "").strip()
            baseline[name] = {"time_s": time_s, "result": result,
                              "ground_truth": ground_truth}
        return baseline

    # Otherwise parse as old multi-column format
    f = open(baseline_csv)
    rows = list(csv.reader(f))
    f.close()

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

    # Optional ground_truth column populated only from filename conventions
    gt_col = None
    for i, h in enumerate(subheaders):
        if h.strip() == "ground_truth":
            gt_col = i
            break

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
        ground_truth = ""
        if gt_col is not None and gt_col < len(row):
            ground_truth = row[gt_col].strip()
        baseline[name] = {"time_s": time_s, "result": result,
                          "ground_truth": ground_truth}
    return baseline


def load_summary(summary_csv: str) -> list[dict]:
    with open(summary_csv) as f:
        return list(csv.DictReader(f))


# Report ordering: correctness first, then the high-priority odeexpr family,
# then ordinary solve→fail (HIGH) and plain timing regressions.
_PRIORITY_ORDER = ["SOUNDNESS", "ODEEXPR-HIGH", "ODEEXPR", "HIGH", "TIMING"]


def priority_rank(priority: str) -> int:
    return _PRIORITY_ORDER.index(priority) if priority in _PRIORITY_ORDER else len(_PRIORITY_ORDER)


def compute_family_comparison(frozen_csv: str, local_summary: list[dict]) -> dict:
    """Compare per-family PAR2 averages between frozen baseline and new local run.

    Every benchmark present in both sets contributes its PAR2 time (actual time
    if solved, PAR2_PENALTY_S=1200 s if TIM/OOM/ERR). This ensures that
    formerly-TIM'd benchmarks that now solve lower the average rather than
    appearing to raise it.

    Families are weighted (FAMILY_WEIGHTS) into a `weighted_overall` PAR2 so the
    high-priority odeexpr families dominate the single-number summary.
    """
    frozen = load_baseline(frozen_csv, "DRPM_0L")
    # The manifest families are not in the frozen DRPM_0L CSV; pull their
    # references from baseline_<family>.csv (new format).
    for path in MANIFEST_BASELINES.values():
        if os.path.exists(path):
            for name, entry in load_baseline(path).items():
                frozen.setdefault(name, entry)

    from collections import defaultdict
    frozen_par2: dict[str, list[float]] = defaultdict(list)
    local_par2: dict[str, list[float]] = defaultdict(list)

    for row in local_summary:
        name = row["benchmark_name"]
        fam = family_of(name)
        if fam is None:
            continue
        base = frozen.get(name)
        if base is None:
            continue
        cur_result = row.get("solver_result", "")
        cur_time = row_time(row)
        local_par2[fam].append(par2_time(cur_result, cur_time))
        frozen_par2[fam].append(par2_time(base["result"], base["time_s"]))

    result = {}
    weighted_local_num = weighted_frozen_num = weight_den = 0.0
    # Iterate families by descending weight for stable, priority-ordered output.
    for fam in sorted(FAMILY_WEIGHTS, key=lambda f: -FAMILY_WEIGHTS[f]):
        lp = local_par2.get(fam, [])
        fp = frozen_par2.get(fam, [])
        if lp and fp:
            local_avg = sum(lp) / len(lp)
            frozen_avg = sum(fp) / len(fp)
            w = FAMILY_WEIGHTS.get(fam, 1)
            weighted_local_num += w * local_avg
            weighted_frozen_num += w * frozen_avg
            weight_den += w
            result[fam] = {
                "frozen_avg_par2": round(frozen_avg, 2),
                "local_avg_par2": round(local_avg, 2),
                "ratio": round(safe_ratio(local_avg, frozen_avg), 3),
                "n": len(lp),
                "weight": w,
            }
        else:
            result[fam] = {"frozen_avg_par2": None, "local_avg_par2": None,
                           "ratio": None, "n": 0, "weight": FAMILY_WEIGHTS.get(fam, 1)}

    if weight_den:
        wl = weighted_local_num / weight_den
        wf = weighted_frozen_num / weight_den
        result["weighted_overall"] = {
            "frozen_avg_par2": round(wf, 2),
            "local_avg_par2": round(wl, 2),
            "ratio": round(wl / wf, 3) if wf else None,
        }
    return result


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("results_dir")
    parser.add_argument("--frozen-baseline", default=None,
                        help="Path to frozen baseline CSV for per-family comparison")
    args = parser.parse_args()

    results_dir = args.results_dir
    state_path = os.path.join(SCRIPT_DIR, "state.json")
    summary_csv = os.path.join(results_dir, "summary.csv")

    with open(state_path) as f:
        state = json.load(f)

    baseline_column = state.get("baseline_column", "DRPM_0L")
    baseline_file = os.path.join(SCRIPT_DIR, "..", state.get("baseline_source", "benchmark/baseline.csv"))
    baseline_file = os.path.normpath(baseline_file)

    baseline = load_baseline(baseline_file, baseline_column)

    # Augment with the frozen baseline (benchmark/baseline.csv): supplies
    # SAT/UNSAT verdicts + ground_truth for benchmarks not in the per-machine
    # local baseline. Without this, SAT↔UNSAT flips on anomaly-list rows
    # (e.g. prostate_h2 — picked by select.py but absent from baseline_local)
    # are invisible because base = None and the row gets skipped.
    frozen_path = os.path.join(SCRIPT_DIR, "baseline.csv")
    if os.path.exists(frozen_path):
        try:
            frozen = load_baseline(frozen_path, "DRPM_0L")
        except Exception:
            frozen = {}
        for name, fentry in frozen.items():
            if name not in baseline:
                # Fallback row: contributes to SAT/UNSAT flip detection only.
                # Timing-regression and solve→timeout branches must not fire on
                # these — frozen times are from a different machine and a TIM
                # locally vs SAT in the frozen ref is not necessarily a code
                # regression.
                baseline[name] = {
                    "time_s": None,
                    "result": fentry.get("result", ""),
                    "ground_truth": fentry.get("ground_truth", ""),
                    "from_frozen": True,
                }
            elif not baseline[name].get("ground_truth"):
                # Local baseline lacks ground_truth for this row — fill from frozen.
                baseline[name]["ground_truth"] = fentry.get("ground_truth", "")

    # Merge the manifest-family baselines as REAL timing references (same
    # machine, same 600 s budget) — unlike the frozen rows above, these carry a
    # usable time_s, so the full PAR2 timing comparison applies to them.
    for path in MANIFEST_BASELINES.values():
        if os.path.exists(path):
            for name, oentry in load_baseline(path).items():
                baseline[name] = oentry  # authoritative for its manifest-family rows

    summary = load_summary(summary_csv)

    regressions = []       # {name, reason, baseline_time, current_time, baseline_result, current_result}
    exceptional_list = []  # {name, reason, baseline_time, current_time}
    resolved_anomalies = []
    correctness_improvements = []  # flips where current matches ground_truth
    undetermined_flips = []        # flips on rows with no ground_truth annotation

    prev_anomalies = set(state.get("anomalies", []))
    prev_exceptional = set(state.get("exceptional", []))

    new_anomalies = set(prev_anomalies)
    new_exceptional = set(prev_exceptional)

    for row in summary:
        name = row["benchmark_name"]
        cur_result = row["solver_result"]
        cur_time = row_time(row)

        base = baseline.get(name)
        if base is None:
            continue  # not in baseline, skip

        base_time = base["time_s"]
        base_result = base["result"]

        # --- SAT↔UNSAT flip: classify against ground_truth ---
        # dReal is sound + delta-complete, so a flip can be either a true
        # soundness regression OR a completeness improvement (the baseline
        # was a spurious δ-witness). Ground truth, when known, distinguishes
        # them. See plan: codac_docs/.../verify-bwd-experiment.
        result_flip = False
        if cur_result in ("SAT", "UNSAT") and base_result in ("SAT", "UNSAT") and cur_result != base_result:
            result_flip = True
            gt = base.get("ground_truth", "").strip()
            if gt == cur_result:
                correctness_improvements.append({
                    "name": name,
                    "reason": f"Flip aligns with ground_truth: baseline={base_result} current={cur_result} GT={gt}",
                    "baseline_time": base_time,
                    "current_time": cur_time,
                    "baseline_result": base_result,
                    "current_result": cur_result,
                    "ground_truth": gt,
                })
                new_anomalies.discard(name)
            elif gt == base_result:
                regressions.append({
                    "name": name,
                    "priority": "SOUNDNESS",
                    "reason": f"Flip disagrees with ground_truth: baseline={base_result} current={cur_result} GT={gt}",
                    "baseline_time": base_time,
                    "current_time": cur_time,
                    "baseline_result": base_result,
                    "current_result": cur_result,
                    "ground_truth": gt,
                })
                new_anomalies.add(name)
            else:
                # No ground_truth annotation (gt == "") or it disagrees with both
                # (shouldn't happen unless annotation is bogus).
                undetermined_flips.append({
                    "name": name,
                    "reason": f"Undetermined flip: baseline={base_result} current={cur_result} (no ground_truth)",
                    "baseline_time": base_time,
                    "current_time": cur_time,
                    "baseline_result": base_result,
                    "current_result": cur_result,
                })
                new_anomalies.add(name)

        from_frozen = base.get("from_frozen", False)

        # --- PAR2 timing comparison ---
        # Skipped for from_frozen rows (different machine, timing unreliable) and
        # correctness flips (already classified above).
        # PAR2 time = actual CPU time if solved, PAR2_PENALTY_S (1200 s) otherwise.
        # This unifies solve→TIM regressions, TIM→solve improvements, and plain
        # timing regressions/speedups into a single ratio check.
        if not from_frozen and not result_flip:
            par2_base = par2_time(base_result, base_time)
            par2_cur  = par2_time(cur_result, cur_time)
            raw_note = (f" [raw: {cur_time:.1f}s]" if cur_time is not None else " [TIM/OOM/ERR]")

            if par2_cur < NEGLIGIBLE_S and par2_base < NEGLIGIBLE_S:
                ratio = 1.0  # both below measurement resolution — not comparable
            else:
                ratio = safe_ratio(par2_cur, par2_base)

            if ratio > REGRESSION_RATIO:
                solve_to_fail = (base_result in ("SAT", "UNSAT")
                                 and cur_result in ("TIM", "OOM", "ERR"))
                # The manifest families (odeexpr_v1/v2) are the high-priority
                # targets: their regressions outrank ordinary ones so the
                # report/skill lead with them.
                if family_of(name) in MANIFEST_FAMILY_NAMES:
                    priority = "ODEEXPR-HIGH" if solve_to_fail else "ODEEXPR"
                else:
                    priority = "HIGH" if solve_to_fail else "TIMING"
                regressions.append({
                    "name": name,
                    "priority": priority,
                    "reason": f"PAR2: {par2_cur:.0f}s vs baseline {par2_base:.0f}s ({ratio:.2f}x){raw_note}",
                    "baseline_time": base_time,
                    "current_time": cur_time,
                    "baseline_result": base_result,
                    "current_result": cur_result,
                })
                new_anomalies.add(name)

            elif ratio < EXCEPTIONAL_RATIO:
                exceptional_list.append({
                    "name": name,
                    "reason": f"PAR2: {par2_cur:.0f}s vs baseline {par2_base:.0f}s ({ratio:.2f}x){raw_note}",
                    "baseline_time": base_time,
                    "current_time": cur_time,
                })
                new_exceptional.add(name)
                new_anomalies.discard(name)

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
            for r in sorted(regressions, key=lambda x: priority_rank(x["priority"])):
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

        if correctness_improvements:
            f.write(f"\n=== CORRECTNESS IMPROVEMENTS ({len(correctness_improvements)}) ===\n")
            f.write("  (current result matches ground_truth; baseline did not)\n")
            for c in correctness_improvements:
                f.write(f"  {c['name']}\n")
                f.write(f"    {c['reason']}\n")

        if undetermined_flips:
            f.write(f"\n=== UNDETERMINED FLIPS ({len(undetermined_flips)}) ===\n")
            f.write("  (SAT↔UNSAT change with no ground_truth; soundness or completeness — cannot tell)\n")
            for u in undetermined_flips:
                f.write(f"  {u['name']}\n")
                f.write(f"    {u['reason']}\n")

        if resolved_anomalies:
            f.write(f"\n=== RESOLVED ANOMALIES ({len(resolved_anomalies)}) ===\n")
            for name in resolved_anomalies:
                f.write(f"  {name}\n")

    # Update state.json
    state["anomalies"] = sorted(new_anomalies)
    state["exceptional"] = sorted(new_exceptional)
    state["last_run"] = datetime.now(timezone.utc).isoformat()
    state["last_run_dir"] = results_dir
    # `correctness_flips` semantics: only SOUNDNESS regressions (flips that
    # disagree with ground_truth). Improvements and undetermined flips are
    # separate buckets — the skill (`.claude/skills/benchmark`) leads with a
    # CORRECTNESS REGRESSION banner only when this list is non-empty.
    soundness_flip_names = [r["name"] for r in regressions if r["priority"] == "SOUNDNESS"]

    state.setdefault("runs", []).append({
        "timestamp": state["last_run"],
        "results_dir": results_dir,
        "n_ran": len(summary),
        "n_regressions": len(regressions),
        "n_exceptional": len(exceptional_list),
        "n_resolved": len(resolved_anomalies),
        "n_correctness_improvements": len(correctness_improvements),
        "n_undetermined_flips": len(undetermined_flips),
        "correctness_flips": soundness_flip_names,
    })

    with open(state_path, "w") as f:
        json.dump(state, f, indent=2)

    # JSON summary to stdout (consumed by Haiku subagent)
    summary_json = {
        "n_ran": len(summary),
        "n_regressions": len(regressions),
        "n_exceptional": len(exceptional_list),
        "n_resolved": len(resolved_anomalies),
        "n_correctness_improvements": len(correctness_improvements),
        "n_undetermined_flips": len(undetermined_flips),
        "correctness_flips": soundness_flip_names,
        "regressions": regressions,
        "exceptional": exceptional_list,
        "correctness_improvements": correctness_improvements,
        "undetermined_flips": undetermined_flips,
        "resolved": resolved_anomalies,
        "anomaly_report": open(report_path).read(),
    }
    if args.frozen_baseline:
        summary_json["family_comparison"] = compute_family_comparison(
            args.frozen_baseline, summary
        )
        summary_json["baseline_sha"] = state.get("baseline_sha", "unknown")
    print(json.dumps(summary_json, indent=2))


if __name__ == "__main__":
    main()
