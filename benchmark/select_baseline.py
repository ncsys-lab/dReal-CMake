#!/usr/bin/env python3
"""Select a stratified sample of benchmarks for re-establishing a local baseline.

Picks up to 10 benchmarks from each flat family (saradc, github, tacas) plus ALL
of the manifest families (odeexpr_v1, odeexpr_v2). Prints TSV (csv_name TAB
filepath) to stdout.
"""
import argparse
import csv
import os
import random
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

from select import SARADC_DIRS, GITHUB_DIR, TACAS_DIR, load_benchmarks, resolve_path
from odeexpr import load_manifest_names, family_of


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default=os.path.join(SCRIPT_DIR, "baseline.csv"),
                        help="CSV with benchmark names to sample from (default: baseline.csv)")
    args = parser.parse_args()

    flat: dict[str, list[str]] = {"saradc": [], "github": [], "tacas": []}
    for n in load_benchmarks(args.input):
        fam = family_of(n)
        if fam in flat:
            flat[fam].append(n)

    # The manifest families (odeexpr_v1/v2) are the high-priority targets: take
    # ALL of each rather than a 10-sample, so the local baseline always covers
    # them wholly. The flat ODE families are sampled at 10 each.
    sample = (
        random.sample(flat["saradc"], min(10, len(flat["saradc"])))
        + random.sample(flat["github"], min(10, len(flat["github"])))
        + random.sample(flat["tacas"], min(10, len(flat["tacas"])))
        + load_manifest_names()
    )

    selected = 0
    for name in sample:
        path = resolve_path(name)
        if path:
            print(f"{name}\t{path}")
            selected += 1
        else:
            print(f"WARN: could not find file for {name}", file=sys.stderr)

    print(f"Selected {selected} benchmarks across all families.", file=sys.stderr)


if __name__ == "__main__":
    main()
