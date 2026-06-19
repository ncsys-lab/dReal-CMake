#!/usr/bin/env bash
# Establish the ode_expressivity (odeexpr) family baseline: run ALL 43 current
# benchmarks at the 600 s global timeout, under nice, measuring CPU time, and
# write benchmark/baseline_odeexpr.csv (new format with a cpu_time_s column).
# Prints OUT_DIR to stdout on completion; all other output goes to stderr.
# Usage: do_baseline_odeexpr.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_DIR/gcc_build/dreal4"

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: dreal4 binary not found at $BINARY" >&2
    exit 1
fi

# Check/start oom_killer (SMT solver resource-limit discipline).
if ! pgrep -f oom_killer.sh > /dev/null 2>&1; then
    nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
fi

SHA=$(git -C "$PROJECT_DIR" rev-parse --short HEAD)
TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="$PROJECT_DIR/benchmark/results/baseline_odeexpr_${SHA}_${TS}"
JOBS_FILE="/tmp/dreal_odeexpr_jobs_${SHA}_${TS}.tsv"

mkdir -p "$OUT_DIR"

python3 "$SCRIPT_DIR/odeexpr.py" --all > "$JOBS_FILE"
bash "$SCRIPT_DIR/run_batch.sh" "$OUT_DIR" "$JOBS_FILE"
python3 "$SCRIPT_DIR/parse_results.py" "$OUT_DIR" >&2

# Save as the odeexpr baseline reference (aggregate.py loads this hardcoded path).
cp "$OUT_DIR/summary.csv" "$SCRIPT_DIR/baseline_odeexpr.csv"

echo "$OUT_DIR"
