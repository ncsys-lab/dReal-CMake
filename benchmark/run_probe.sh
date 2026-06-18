#!/usr/bin/env bash
# Run the ODE-heavy probe set and compare against probe_baseline.csv.
# Fast, sensitive signal for CAPD/ODE-contractor changes (the gate ~30-set is
# weak for CAPD because many stratified benchmarks are trivial-flow).
#
# Usage:
#   bash run_probe.sh                 # run probe, compare to probe_baseline.csv
#   bash run_probe.sh --establish     # run probe, OVERWRITE probe_baseline.csv (no compare)
#
# Prints OUT_DIR on the last line.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_DIR/gcc_build/dreal4"

[[ -x "$BINARY" ]] || { echo "ERROR: dreal4 not found at $BINARY" >&2; exit 1; }
pgrep -f oom_killer.sh >/dev/null 2>&1 || nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &

ESTABLISH=0
[[ "${1:-}" == "--establish" ]] && ESTABLISH=1

SHA=$(git -C "$PROJECT_DIR" rev-parse --short HEAD)
TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="$PROJECT_DIR/benchmark/results/probe_${SHA}_${TS}"
mkdir -p "$OUT_DIR"

bash "$SCRIPT_DIR/run_batch.sh" "$OUT_DIR" "$SCRIPT_DIR/probe_odes.tsv" >&2
python3 "$SCRIPT_DIR/parse_results.py" "$OUT_DIR" >&2

if [[ $ESTABLISH -eq 1 ]]; then
    cp "$OUT_DIR/summary.csv" "$SCRIPT_DIR/probe_baseline.csv"
    echo "Established probe_baseline.csv from $OUT_DIR" >&2
else
    python3 "$SCRIPT_DIR/probe_compare.py" "$OUT_DIR/summary.csv" >&2 || true
fi

echo "$OUT_DIR"
