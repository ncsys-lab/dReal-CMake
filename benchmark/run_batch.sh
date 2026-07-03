#!/usr/bin/env bash
# Usage: run_batch.sh <output_dir> [jobs_file]
# Reads TSV pairs (csv_name <TAB> filepath) from stdin or jobs_file, one per line.
# Runs each benchmark in parallel with gtime -v, nice -n 1, and a 600s timeout.
# Timing metric is CPU time (User+System) from gtime, not wall clock — the
# machine is multi-tenant, so wall clock is noisy; `timeout` stays wall-clock.
# Outputs per-benchmark using csv_name (minus .smt2) as the label:
#   <label>.stdout, <label>.solver_log, <label>.gtime, <label>.exit

# Defaults to the HEAD arm64 build; override with DREAL_BINARY to benchmark a
# different solver build (e.g. /usr/local/bin/dreal4_cav26) over the same jobs.
BINARY="${DREAL_BINARY:-$(dirname "$0")/../gcc_build/dreal4}"
# Extra solver flags injected into every invocation (word-split intentionally),
# so one binary can be swept over flag values, e.g.
#   DREAL_ARGS="--ode-taylor-order 12 --ode-c0-set tripleton" run_batch.sh ...
DREAL_ARGS="${DREAL_ARGS:-}"
# Per-job wall-clock timeout (seconds). Default 600 matches the corpus TIM
# cutoff; a sweep's cheap OFAT probe phase may set a tighter cap (a config that
# pushes a fast benchmark far past its default time is already "worse"). The
# final full-corpus confirmation keeps 600.
TIMEOUT="${TIMEOUT:-600}"
OUT="$1"
mkdir -p "$OUT"
[[ -n "${2:-}" ]] && exec < "$2"

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: dreal4 binary not found at $BINARY" >&2
    exit 1
fi

MAX_JOBS=12

while IFS=$'\t' read -r csv_name filepath; do
    [[ -z "$csv_name" || -z "$filepath" ]] && continue
    label="${csv_name%.smt2}"

    while (( $(jobs -r | wc -l) >= MAX_JOBS )); do
        sleep 0.1
    done

    (
        gtime -v -o "$OUT/${label}.gtime" nice -n 1 timeout "$TIMEOUT" "$BINARY" $DREAL_ARGS "$filepath" \
            > "$OUT/${label}.stdout" \
            2> "$OUT/${label}.solver_log"
        echo $? > "$OUT/${label}.exit"
    ) &
done
wait
echo "All benchmarks complete." >&2
