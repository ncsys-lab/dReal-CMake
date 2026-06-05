#!/usr/bin/env python3
"""Select benchmarks for a run: 8 random + all current anomalies.

Usage: python3 select.py [--n N] [--seed SEED]
Prints full file paths, one per line, to stdout.
"""
import argparse
import csv
import json
import os
import random
import sys

BENCHMARK_DIR = "/Users/kunalsheth/Documents/new_dreal/nraode_to_nra"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# SARADC files are split across two directories; search both in order.
SARADC_DIRS = [
    os.path.join(BENCHMARK_DIR, "SARADC_tueoct14"),
    os.path.join(BENCHMARK_DIR, "REB_SAR_k1_dec9"),
]
GITHUB_DIR = os.path.join(BENCHMARK_DIR, "drealgithub_sunoct5", "rolled")
TACAS_DIR  = os.path.join(BENCHMARK_DIR, "VNAMSCwI_satoct11", "rolled")


def resolve_path(bench_name: str) -> str | None:
    if bench_name.startswith("github_oct5_"):
        filename = bench_name[len("github_oct5_"):]
        p = os.path.join(GITHUB_DIR, filename)
        return p if os.path.exists(p) else None
    if bench_name.startswith("tacas_c2e2_"):
        filename = bench_name[len("tacas_c2e2_"):]
        p = os.path.join(TACAS_DIR, filename)
        return p if os.path.exists(p) else None
    if bench_name.startswith("1mhz_"):
        for d in SARADC_DIRS:
            p = os.path.join(d, bench_name)
            if os.path.exists(p):
                return p
        return None
    return None


def load_benchmarks(baseline_csv: str) -> list[str]:
    names = []
    with open(baseline_csv) as f:
        reader = csv.reader(f)
        rows = list(reader)
    # Row 0: group headers, Row 1: sub-headers, Row 2: index label, Row 3+: data
    for row in rows[3:]:
        if row and row[0].strip():
            names.append(row[0].strip())
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--n", type=int, default=8, help="random benchmarks to add (not counting anomalies)")
    parser.add_argument("--seed", type=int, default=None)
    args = parser.parse_args()

    baseline_csv = os.path.join(SCRIPT_DIR, "baseline.csv")
    state_path = os.path.join(SCRIPT_DIR, "state.json")

    all_names = load_benchmarks(baseline_csv)

    with open(state_path) as f:
        state = json.load(f)
    anomalies = set(state.get("anomalies", []))

    # state.json stores names without .smt2; baseline.csv names include .smt2 —
    # normalize for comparison but preserve the original name for output/path resolution.
    anomaly_names = [n for n in all_names if n.removesuffix(".smt2") in anomalies]
    pool = [n for n in all_names if n.removesuffix(".smt2") not in anomalies]

    rng = random.Random(args.seed)

    # Resolve paths eagerly so we can filter out missing files without under-delivering
    resolved_pool = [(n, resolve_path(n)) for n in pool]
    resolvable = [(n, p) for n, p in resolved_pool if p]
    unresolvable = [n for n, p in resolved_pool if not p]
    for n in unresolvable:
        print(f"WARN: could not find file for {n}", file=sys.stderr)

    sample_size = min(args.n, len(resolvable))
    selected_pool = rng.sample(resolvable, sample_size)

    # Always include anomalies (resolved)
    anomaly_paths = [(n, resolve_path(n)) for n in anomaly_names]
    for n, p in anomaly_paths:
        if not p:
            print(f"WARN: anomaly file missing on disk: {n}", file=sys.stderr)

    all_selected = selected_pool + [(n, p) for n, p in anomaly_paths if p]

    for name, path in all_selected:
        # Output TSV: csv_name <TAB> filepath
        # run_batch.sh uses csv_name as the output label so aggregate.py can match baseline.
        print(f"{name}\t{path}")

    print(
        f"Selected {len(all_selected)} benchmarks "
        f"({len([x for x in anomaly_paths if x[1]])} anomalies, {sample_size} random); "
        f"{len(unresolvable)} in pool not found on disk (skipped).",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
