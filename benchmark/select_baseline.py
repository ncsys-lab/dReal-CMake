#!/usr/bin/env python3
"""Select a stratified sample of benchmarks for re-establishing a local baseline.

Picks up to 10 benchmarks from each of the three families (saradc, github, tacas)
for a ~30-benchmark run. Prints TSV (csv_name TAB filepath) to stdout.
"""
import argparse
import csv
import os
import random
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

from select import SARADC_DIRS, GITHUB_DIR, TACAS_DIR, load_benchmarks, resolve_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default=os.path.join(SCRIPT_DIR, "baseline.csv"),
                        help="CSV with benchmark names to sample from (default: baseline.csv)")
    args = parser.parse_args()
    all_names = load_benchmarks(args.input)

    fams: dict[str, list[str]] = {"saradc": [], "github": [], "tacas": []}
    for n in all_names:
        if n.startswith("1mhz_"):
            fams["saradc"].append(n)
        elif n.startswith("github_oct5_"):
            fams["github"].append(n)
        elif n.startswith("tacas_c2e2_"):
            fams["tacas"].append(n)

    sample = (
        random.sample(fams["saradc"], min(10, len(fams["saradc"])))
        + random.sample(fams["github"], min(10, len(fams["github"])))
        + random.sample(fams["tacas"], min(10, len(fams["tacas"])))
    )

    selected = 0
    for name in sample:
        path = resolve_path(name)
        if path:
            print(f"{name}\t{path}")
            selected += 1
        else:
            print(f"WARN: could not find file for {name}", file=sys.stderr)

    print(f"Selected {selected} benchmarks across 3 families.", file=sys.stderr)


if __name__ == "__main__":
    main()
