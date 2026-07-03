#!/usr/bin/env bash
# Sweep ONE dreal4 binary over multiple flag configurations on the SAME jobs.
#
# Usage: do_sweep.sh NAME1="<flags1>" NAME2="<flags2>" ...
#   Each positional arg is  label=flagstring  (flagstring may be empty for the
#   default/baseline point, e.g.  base=  ). The label names the per-config output
#   subdir and the compare_solvers.py column; the flagstring is injected verbatim
#   via the per-job flags column, e.g.  o12="--ode-taylor-order 12".
#
#   JOBS         env: TSV jobs file (default = benchmark/probe_odes.tsv, the OFAT
#                     probe set). Point at select.py --family ... --all output for
#                     a full-corpus confirmation run.
#   DREAL_BINARY env: solver to sweep (default = ../gcc_build/dreal4).
#   MAXJOBS      env: pool width (default 12, the project's standard concurrency).
#   TIMEOUT      env: per-job wall cap in seconds (default 600).
#
# POOLED execution: all (config x benchmark) pairs run in ONE shuffled MAXJOBS-way
# queue, NOT one batch per config. This is the fix for the idle-tail waste of
# per-config batches — an 18-job probe drains to its 2-3 long-poles (e.g. a k256
# thermostat) while 13 cores sit idle; pooling overlaps a slow config's long-pole
# with other configs' fast jobs, so the cores stay full. Crucially this does NOT
# oversubscribe: at most MAXJOBS solver processes run at once, each nice -n 1 on
# its own core, so the per-process CPU-time metric stays accurate (same per-core
# fairness as run_batch's 12-way, just better packed). The shuffle spreads the
# long-poles across the run so the only thin tail is the final ~MAXJOBS jobs.
# Reuses parse_results.py + compare_solvers.py for the table.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
[[ $# -ge 1 ]] || { echo "usage: do_sweep.sh NAME='flags' NAME='flags' ..." >&2; exit 1; }

BINARY="${DREAL_BINARY:-$SCRIPT_DIR/../gcc_build/dreal4}"
[[ -x "$BINARY" ]] || { echo "ERROR: solver not executable: $BINARY" >&2; exit 1; }
JOBS="${JOBS:-$SCRIPT_DIR/probe_odes.tsv}"
[[ -f "$JOBS" ]] || { echo "ERROR: jobs file not found: $JOBS" >&2; exit 1; }
MAXJOBS="${MAXJOBS:-12}"
TIMEOUT="${TIMEOUT:-600}"

# oom_killer daemon (SSD-protection; SMT solvers can swap indefinitely).
if ! pgrep -f oom_killer.sh > /dev/null 2>&1; then
    nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
fi

TS=$(date +%Y%m%d_%H%M%S)
OUT="$SCRIPT_DIR/results/sweep_${TS}"
mkdir -p "$OUT"

# Build the cartesian (config x benchmark) job list as
#   config <TAB> flags <TAB> csv <TAB> path
# Flags for the `base` point are empty; an empty TSV field is collapsed by
# `read` (tab is whitespace-class IFS), which would shift `path` out of range
# and silently drop that config. So empty flags are encoded as the sentinel
# NONE (decoded back to "" in the runner) — every field is then non-empty.
# (bash 3.2 on macOS has no associative arrays, hence a TSV column not a map.)
labels=()
JOBS4="$OUT/_pool_jobs.tsv"; : > "$JOBS4"
for spec in "$@"; do
    if [[ "$spec" == *=* ]]; then label="${spec%%=*}"; flags="${spec#*=}"; else label="$spec"; flags=""; fi
    labels+=("$label")
    [[ -z "$flags" ]] && flags="NONE"
    mkdir -p "$OUT/$label"
    while IFS=$'\t' read -r csv path; do
        [[ -z "$csv" || -z "$path" ]] && continue
        printf '%s\t%s\t%s\t%s\n' "$label" "$flags" "$csv" "$path" >> "$JOBS4"
    done < "$JOBS"
done
NJOBS=$(grep -c . "$JOBS")
NRUNS=$(grep -c . "$JOBS4")
echo "sweep: ${#labels[@]} configs x $NJOBS jobs = $NRUNS runs, ${MAXJOBS}-way pool, ${TIMEOUT}s cap -> $OUT" >&2

# Portable shuffle (no GNU shuf on macOS): prefix a random key, sort, strip it.
SHUF="$OUT/_pool_shuffled.tsv"
awk 'BEGIN{srand()} {print rand()"\t"$0}' "$JOBS4" | sort -n | cut -f2- > "$SHUF"

# One MAXJOBS-way pool over ALL pairs.
while IFS=$'\t' read -r config flags csv path; do
    [[ -z "$config" || -z "$path" ]] && continue
    [[ "$flags" == "NONE" ]] && flags=""
    label="${csv%.smt2}"
    d="$OUT/$config"
    while (( $(jobs -r | wc -l) >= MAXJOBS )); do sleep 0.1; done
    (
        gtime -v -o "$d/${label}.gtime" nice -n 1 timeout "$TIMEOUT" "$BINARY" $flags "$path" \
            > "$d/${label}.stdout" \
            2> "$d/${label}.solver_log"
        echo $? > "$d/${label}.exit"
    ) &
done < "$SHUF"
wait
echo "pool complete." >&2

compare_args=()
for label in "${labels[@]}"; do
    python3 "$SCRIPT_DIR/parse_results.py" "$OUT/$label" >&2
    compare_args+=("$label=$OUT/$label/summary.csv")
done

python3 "$SCRIPT_DIR/compare_solvers.py" "${compare_args[@]}" | tee "$OUT/compare.txt" >&2
echo "$OUT"
