#!/usr/bin/env python3
"""Parse a benchmark results directory into summary.csv.

Usage: python3 parse_results.py <results_dir>
Produces <results_dir>/summary.csv with columns:
  benchmark_name, solver_result, cpu_time_s, wall_time_s, max_rss_kb, exit_code

`cpu_time_s` (User+System time) is the primary timing metric — the machine is
multi-tenant, so wall clock is noisy and unfairly penalizes a descheduled run.
`wall_time_s` is kept for reference and as a TIM backup signal.
"""
import csv
import os
import re
import sys


def parse_wall_time(gtime_text: str) -> float | None:
    m = re.search(r"Elapsed \(wall clock\) time.*?:\s*(\d+):(\d+\.\d+)", gtime_text)
    if m:
        return int(m.group(1)) * 60 + float(m.group(2))
    m = re.search(r"Elapsed \(wall clock\) time.*?:\s*(\d+):(\d+):(\d+\.\d+)", gtime_text)
    if m:
        return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))
    return None


def parse_cpu_time(gtime_text: str) -> float | None:
    """CPU time = User time + System time (seconds), from gtime -v output."""
    u = re.search(r"User time \(seconds\):\s*([\d.]+)", gtime_text)
    s = re.search(r"System time \(seconds\):\s*([\d.]+)", gtime_text)
    if u and s:
        return float(u.group(1)) + float(s.group(1))
    return None


def parse_rss(gtime_text: str) -> int | None:
    m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", gtime_text)
    return int(m.group(1)) if m else None


def parse_solver_result(stdout_text: str, exit_code: int, wall_time_s: float | None) -> str:
    if exit_code == 137:
        return "OOM"
    if exit_code == 124 or (wall_time_s is not None and wall_time_s > 595):
        return "TIM"
    if "delta-sat" in stdout_text:
        return "SAT"
    if "unsat" in stdout_text.lower():
        return "UNSAT"
    if exit_code != 0:
        return "ERR"
    return "ERR"


def parse_results_dir(results_dir: str) -> list[dict]:
    rows = []
    for fname in sorted(os.listdir(results_dir)):
        if not fname.endswith(".exit"):
            continue
        name = fname[:-5]  # strip .exit

        exit_path = os.path.join(results_dir, fname)
        stdout_path = os.path.join(results_dir, name + ".stdout")
        gtime_path = os.path.join(results_dir, name + ".gtime")

        exit_code = int(open(exit_path).read().strip())
        stdout_text = open(stdout_path).read() if os.path.exists(stdout_path) else ""
        gtime_text = open(gtime_path).read() if os.path.exists(gtime_path) else ""

        wall_time = parse_wall_time(gtime_text)
        cpu_time = parse_cpu_time(gtime_text)
        max_rss = parse_rss(gtime_text)
        result = parse_solver_result(stdout_text, exit_code, wall_time)

        rows.append({
            "benchmark_name": name,
            "solver_result": result,
            "cpu_time_s": f"{cpu_time:.2f}" if cpu_time is not None else "",
            "wall_time_s": f"{wall_time:.2f}" if wall_time is not None else "",
            "max_rss_kb": str(max_rss) if max_rss is not None else "",
            "exit_code": str(exit_code),
        })
    return rows


def main():
    if len(sys.argv) < 2:
        print("Usage: parse_results.py <results_dir>", file=sys.stderr)
        sys.exit(1)

    results_dir = sys.argv[1]
    rows = parse_results_dir(results_dir)

    out_path = os.path.join(results_dir, "summary.csv")
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["benchmark_name", "solver_result", "cpu_time_s", "wall_time_s", "max_rss_kb", "exit_code"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} rows to {out_path}")


if __name__ == "__main__":
    main()
