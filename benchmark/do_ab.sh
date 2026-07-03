#!/usr/bin/env bash
# A/B two dreal4 binaries over the SAME job set, with honest timing.
#
# Usage: do_ab.sh BIN_A BIN_B [jobs_file]
#   BIN_A, BIN_B : two dreal4 builds to compare (e.g. a committed-HEAD build vs a
#                  working-tree build, or /usr/local/bin/dreal4_cav26).
#   jobs_file    : TSV (csv_name <TAB> filepath), one per line. Default = the full
#                  ODE-family corpus (select.py --family github,tacas,saradc --all).
#
# Reuses the standard harness (run_batch.sh honors DREAL_BINARY; parse_results.py;
# compare_solvers.py) instead of a freelance script, so an A/B is reproducible.
#
# The two binaries run SEQUENTIALLY (A fully finishes before B starts), never
# concurrently: run_batch.sh already parallelizes 12-way internally, and the 600 s
# TIM cutoff is WALL-clock — overlapping two batches would starve jobs and turn
# real solves into false TIMs (the measurement artifact this avoids). Timing is
# CPU time (user+sys) per the project methodology; wall is a reference/backup.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_A="${1:?usage: do_ab.sh BIN_A BIN_B [jobs_file]}"
BIN_B="${2:?usage: do_ab.sh BIN_A BIN_B [jobs_file]}"
JOBS="${3:-}"

for b in "$BIN_A" "$BIN_B"; do
    [[ -x "$b" ]] || { echo "ERROR: not executable: $b" >&2; exit 1; }
done

LA="$(basename "$BIN_A")"
LB="$(basename "$BIN_B")"
[[ "$LA" == "$LB" ]] && { LA="A_$LA"; LB="B_$LB"; }  # disambiguate identical names

# oom_killer daemon (SSD-protection; SMT solvers can swap indefinitely).
if ! pgrep -f oom_killer.sh > /dev/null 2>&1; then
    nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
fi

TS=$(date +%Y%m%d_%H%M%S)
OUT="$SCRIPT_DIR/results/ab_${TS}"
mkdir -p "$OUT"

if [[ -z "$JOBS" ]]; then
    JOBS="$OUT/jobs.tsv"
    python3 "$SCRIPT_DIR/select.py" --family github,tacas,saradc --all > "$JOBS"
fi
NJOBS=$(grep -c . "$JOBS")
echo "A/B over $NJOBS jobs: A=$LA  B=$LB  ->  $OUT" >&2

run_side() {
    local label="$1" bin="$2"
    local out="$OUT/$label"
    echo "=== running $label ($bin) ===" >&2
    DREAL_BINARY="$bin" bash "$SCRIPT_DIR/run_batch.sh" "$out" "$JOBS"
    python3 "$SCRIPT_DIR/parse_results.py" "$out" >&2
}

run_side "$LA" "$BIN_A"   # A fully completes ...
run_side "$LB" "$BIN_B"   # ... before B starts (no cross-batch contention)

python3 "$SCRIPT_DIR/compare_solvers.py" \
    "$LA=$OUT/$LA/summary.csv" "$LB=$OUT/$LB/summary.csv" | tee "$OUT/compare.txt" >&2

echo "$OUT"
