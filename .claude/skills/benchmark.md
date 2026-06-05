# /benchmark skill

Run a quick regression benchmark batch and report findings.

## Steps

1. **Tell the user** "Benchmarks are running — I'll report back when done."

2. **Spawn a Haiku subagent** to handle everything. Spawn with this prompt (fill in the literal git SHA before spawning):

---
**Subagent prompt** (replace `<SHA>` with the actual short SHA from `git -C /Users/kunalsheth/Documents/new_dreal/dreal4-cmake rev-parse --short HEAD`):

You are running regression benchmarks for the dReal4 SMT solver project at `/Users/kunalsheth/Documents/new_dreal/dreal4-cmake`. Complete all steps below and return a formatted summary as your final message.

**Step A — Check oom_killer.sh** (required before any SMT solver runs):
```bash
pgrep -f oom_killer.sh || nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
```

**Step B — Create output dir:**
```bash
TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="/Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/results/run_<SHA>_${TS}"
mkdir -p "$OUT_DIR"
echo "$OUT_DIR"
```

**Step C — Run benchmarks** (run in the **foreground** with a 420000ms timeout — `run_batch.sh` parallelizes internally via `&`/`wait`, so this Bash call blocks until all jobs are done; do NOT use `run_in_background` here):
```bash
cd /Users/kunalsheth/Documents/new_dreal/dreal4-cmake
python3 benchmark/select.py 2>/dev/stderr | bash benchmark/run_batch.sh "$OUT_DIR"
```
Warnings about missing files on stderr are normal.

**Step D — Parse results:**
```bash
python3 /Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/parse_results.py "$OUT_DIR"
```

**Step E — Aggregate and capture JSON:**
```bash
python3 /Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/aggregate.py "$OUT_DIR"
```
Capture the full JSON output.

**Step F — Check for correctness regressions:** If `correctness_flips` in the JSON is non-empty, prepend your summary with:
> **CORRECTNESS REGRESSION**: The following benchmarks changed SAT/UNSAT result: [names]. This is a soundness or completeness bug.

**Step G — Read anomaly report:**
```bash
cat "$OUT_DIR/anomaly_report.txt"
```

**Step H — Return a formatted summary** as your final message:
- First line: `N ran, M regressions, K exceptional` (counts from the JSON)
- Then 2–4 sentences: (1) overall health, (2) notable timing changes, (3) whether exceptional speedups look like real wins or noise
- End with one sentence: does the user need to investigate anything before continuing development?
- Last line: `anomaly_report: $OUT_DIR/anomaly_report.txt`

Be terse. Do not narrate your steps — only return the final formatted summary.

---

3. **Relay** the subagent's summary verbatim to the user.

## Notes
- The benchmark binary is at `gcc_build/dreal4` — if it doesn't exist, remind the user to run `./BUILD.sh` first.
- `state.json` is updated automatically by aggregate.py. Anomalies accumulate across rounds and are always re-run.
- If the `exceptional` list grows large across multiple rounds, recommend running `/benchmark-baseline` to refresh the frozen reference.
