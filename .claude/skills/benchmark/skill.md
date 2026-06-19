---
name: benchmark
description: Run a quick regression benchmark batch (~8-12 benchmarks) for the dReal4 solver and report timing regressions or exceptional speedups. Use after any meaningful code change for regression detection.
---

# /benchmark skill

1. Tell the user: "Benchmarks are running — I'll report back when done."

2. Spawn a Haiku subagent with this exact prompt:

---
Run the dReal4 benchmark script (foreground, 1500000ms timeout):
```bash
bash /Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/do_benchmark.sh
```
The script prints OUT_DIR to stdout when done. Use the Read tool to read `<OUT_DIR>/aggregate.json`.

Return a formatted summary as your only output:
- If `correctness_flips` is non-empty, lead with: **CORRECTNESS REGRESSION**: [names] changed SAT/UNSAT result.
- Then, if any regression has priority `ODEEXPR-HIGH`/`ODEEXPR` (the high-priority ode_expressivity family — names start `odeexpr_`), lead with those next: **ODEEXPR REGRESSION**: [names]. odeexpr is weighted ~3× the other families, so treat its regressions as more serious and its speedups as more meaningful.
- First line: `N ran, M regressions, K exceptional`
- 2–4 sentences: (1) overall health, (2) notable timing changes using PAR2 scores — PAR2 is now CPU time (user+sys), 600 s timeout / 1200 s penalty (e.g. "PAR2: 50 s vs 1200 s (0.04×, formerly TIM)"), (3) whether exceptional speedups look real or noise
- One sentence: what needs investigation before continuing, if anything
- Last line: `anomaly_report: <OUT_DIR>/anomaly_report.txt`

Be terse. Only return the final summary — no narration.

---

3. Relay the subagent's summary verbatim to the user.

## Notes
- Binary at `gcc_build/dreal4` — if missing, ask user to run `./BUILD.sh` first.
- If exceptional list grows large across rounds, suggest `/benchmark-baseline`.
