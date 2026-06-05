#!/usr/bin/env python3
"""Update state.json to point to the local baseline. Called from do_baseline.sh."""
import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def main():
    sha = sys.argv[1] if len(sys.argv) > 1 else "unknown"
    path = os.path.join(SCRIPT_DIR, "state.json")
    with open(path) as f:
        state = json.load(f)
    state["baseline_source"] = "benchmark/baseline_local.csv"
    state["baseline_column"] = "wall_time_s"
    state["baseline_sha"] = sha
    with open(path, "w") as f:
        json.dump(state, f, indent=2)


if __name__ == "__main__":
    main()
