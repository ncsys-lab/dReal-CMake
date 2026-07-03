#!/usr/bin/env bash
# Usage: run_dreal3.sh <output_dir> [jobs_file]
# Reads TSV pairs (csv_name <TAB> filepath) from stdin or jobs_file, one per line.
# Runs each benchmark through dReal v3.16.12 in Docker (dreal3:1.1), in parallel,
# with a 600s wall timeout. Emits per-benchmark <label>.{stdout,gtime,exit} in the
# same shape parse_results.py expects, so the same parser produces summary.csv.
#
# dReal3 input adaptation (semantics-preserving — it predates these constructs):
#   * prepend `(set-logic QF_NRA)`  (dReal3 segfaults without a logic)
#   * strip the in-file `(set-option :precision P)` line (dReal3 segfaults on it)
#     and pass P via the `--precision` flag instead
#   * strip `(get-model)` (dReal3 v3.16 has no such command)
# Timing is the IN-CONTAINER CPU time (User+System) via bash's `time`; host gtime
# around `docker run` would only measure the docker client, not the solver.
#
# CRITICAL: the 600 s timeout is enforced INSIDE the container (`timeout` before
# ./dReal). On macOS, a host-side `timeout` around `docker run` only kills the
# docker *client* — the container keeps running in the VM as a zombie at 100%
# CPU. The host `timeout 700` is just a backstop, and a post-run sweep removes
# any container that still slipped through.

IMAGE="dreal3:1.1"
OUT="$1"
mkdir -p "$OUT"
[[ -n "${2:-}" ]] && exec < "$2"

# Keep well under the VM's 12 CPUs so wall≈CPU for these single-threaded solves
# and the box isn't saturated (which is what let zombie containers pile up).
MAX_JOBS=6

run_one() {
    local csv_name="$1" filepath="$2"
    local label="${csv_name%.smt2}"
    local prec
    prec=$(grep -oE ':precision[[:space:]]+[0-9.eE+-]+' "$filepath" | grep -oE '[0-9.eE+-]+$' | head -1)
    [[ -z "$prec" ]] && prec=0.001

    local errf="$OUT/${label}.d3err"
    # Transform on the host, pipe into the container; capture in-container CPU
    # via bash `time` (TIMEFORMAT prints a one-line UCPU=/SCPU= record to stderr).
    { echo "(set-logic QF_NRA)"; grep -vE '\(set-option[^)]*:precision|\(get-model' "$filepath"; } \
        | timeout 700 docker run --platform linux/amd64 --rm -i "$IMAGE" \
            bash -c "TIMEFORMAT='UCPU=%U SCPU=%S'; time timeout -s KILL 600 ./dReal --in --precision $prec" \
            > "$OUT/${label}.stdout" 2> "$errf"
    local rc=$?
    # dReal3 ignores SIGTERM, so the timeout uses SIGKILL → exit 137. These
    # benchmarks are tiny (~10 MiB, single-threaded), so 137 is the timeout
    # kill, not OOM — normalize to 124 so parse_results.py classifies it TIM.
    [[ $rc -eq 137 ]] && rc=124
    echo "$rc" > "$OUT/${label}.exit"

    # Synthesize a gtime-shaped file from the in-container CPU record so
    # parse_results.py can read User/System time uniformly.
    local u s
    u=$(grep -oE 'UCPU=[0-9.]+' "$errf" | tail -1 | cut -d= -f2)
    s=$(grep -oE 'SCPU=[0-9.]+' "$errf" | tail -1 | cut -d= -f2)
    if [[ -n "$u" && -n "$s" ]]; then
        printf '\tUser time (seconds): %s\n\tSystem time (seconds): %s\n' "$u" "$s" > "$OUT/${label}.gtime"
    else
        : > "$OUT/${label}.gtime"   # timeout/crash: no CPU record (TIM via exit 124)
    fi
}

while IFS=$'\t' read -r csv_name filepath; do
    [[ -z "$csv_name" || -z "$filepath" ]] && continue
    while (( $(jobs -r | wc -l) >= MAX_JOBS )); do
        sleep 0.2
    done
    run_one "$csv_name" "$filepath" &
done
wait

# Safety net: remove any dreal3 container still alive (e.g. a hung docker client
# that the in-container timeout + host backstop both somehow missed).
LEFT=$(docker ps -q --filter ancestor="$IMAGE" 2>/dev/null)
[[ -n "$LEFT" ]] && { echo "WARN: force-removing leftover containers: $LEFT" >&2; for c in $LEFT; do docker rm -f "$c" >/dev/null 2>&1; done; }
echo "All dReal3 benchmarks complete." >&2
