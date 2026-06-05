#!/usr/bin/env bash
# Usage: run_batch.sh <output_dir>
# Reads TSV pairs (csv_name <TAB> filepath) from stdin, one per line.
# Runs each benchmark in parallel with gtime -v and a 300s timeout.
# Outputs per-benchmark using csv_name (minus .smt2) as the label:
#   <label>.stdout, <label>.solver_log, <label>.gtime, <label>.exit

BINARY="$(dirname "$0")/../gcc_build/dreal4"
OUT="$1"
mkdir -p "$OUT"

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: dreal4 binary not found at $BINARY" >&2
    exit 1
fi

while IFS=$'\t' read -r csv_name filepath; do
    [[ -z "$csv_name" || -z "$filepath" ]] && continue
    label="${csv_name%.smt2}"
    (
        gtime -v -o "$OUT/${label}.gtime" timeout 300 "$BINARY" "$filepath" \
            > "$OUT/${label}.stdout" \
            2> "$OUT/${label}.solver_log"
        echo $? > "$OUT/${label}.exit"
    ) &
done
wait
echo "All benchmarks complete." >&2
