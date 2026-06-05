# /benchmark skill

Run a quick regression benchmark batch and report findings.

## Steps

1. **Check oom_killer.sh** (required per smt-solver-limits.md):
   ```bash
   pgrep -f oom_killer.sh || nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
   ```

2. **Get git SHA and create output dir**:
   ```bash
   SHA=$(git -C /Users/kunalsheth/Documents/new_dreal/dreal4-cmake rev-parse --short HEAD)
   TS=$(date +%Y%m%d_%H%M%S)
   OUT_DIR="/Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/results/run_${SHA}_${TS}"
   mkdir -p "$OUT_DIR"
   ```

3. **Select and run benchmarks** — `select.py` outputs TSV (csv_name TAB filepath), piped directly to `run_batch.sh` via stdin (will take up to 5 minutes):
   ```bash
   cd /Users/kunalsheth/Documents/new_dreal/dreal4-cmake
   python3 benchmark/select.py 2>/dev/stderr | bash benchmark/run_batch.sh "$OUT_DIR"
   ```
   Warnings about missing files go to stderr; they are not a problem.

4. **Parse results**:
   ```bash
   python3 benchmark/parse_results.py "$OUT_DIR"
   ```

5. **Aggregate and detect anomalies**:
   ```bash
   AGGREGATE_JSON=$(python3 benchmark/aggregate.py "$OUT_DIR")
   ```
   Capture the JSON output.

6. **Escalate correctness regressions immediately** — before spawning any subagent, if `correctness_flips` in the JSON is non-empty, report this to the user directly:
   > **CORRECTNESS REGRESSION**: The following benchmarks changed SAT/UNSAT result: [names]. This is a soundness or completeness bug and must be investigated before proceeding.

7. **Spawn Haiku subagent** for interpretation. Pass it:
   - The full JSON aggregate output
   - The `anomaly_report.txt` content from `$OUT_DIR/anomaly_report.txt`
   - This instruction: "You are interpreting dReal4 SMT solver benchmark results. Provide 2-4 sentences summarizing: (1) overall health (any regressions?), (2) notable timing changes, (3) whether the exceptional speedups look like real wins or measurement noise. End with a single recommendation: does the user need to investigate anything before continuing development? Be terse."

8. **Report** the subagent's summary back to the user. Also include:
   - `N ran, M regressions, K exceptional` as a one-line header
   - The anomaly_report.txt path for reference if the user wants details

## Notes
- The benchmark binary is at `gcc_build/dreal4` — if it doesn't exist, remind the user to run `./BUILD.sh` first.
- `state.json` is updated automatically by aggregate.py. Anomalies accumulate across rounds and are always re-run.
- If the `exceptional` list grows large across multiple rounds, recommend running `/benchmark-baseline` to refresh the frozen reference.
