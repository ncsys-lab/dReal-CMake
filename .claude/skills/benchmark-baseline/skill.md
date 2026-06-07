---
name: benchmark-baseline
description: Re-establish a local performance baseline by running ~30 stratified dReal4 benchmarks with the current binary. Use before merging branches or when the exceptional list grows stale.
---

# /benchmark-baseline skill

1. **Confirm with user first:**
   > This will run ~30 benchmarks (~15-20 min) and update `benchmark/state.json` to use a local baseline. Proceed?

   Wait for confirmation before continuing.

2. Tell the user: "Baseline run starting — I'll report back when done."

3. Spawn a Haiku subagent with this exact prompt:

---
Run the dReal4 baseline script (foreground, 1200000ms timeout):
```bash
bash /Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/do_baseline.sh
```
The script prints OUT_DIR to stdout when done. Use the Read tool to read `<OUT_DIR>/aggregate.json`. Do not run any other commands or read any other files.

The `aggregate.json` contains a `family_comparison` key with per-family frozen vs local averages, and a `baseline_sha` key.

Return a formatted summary as your only output:
- Header: `Baseline established from <baseline_sha> — <n_ran> benchmarks across 3 families`
- Table: one row per family from `family_comparison` — family | n | frozen PAR2 avg | local PAR2 avg | ratio (keys: `frozen_avg_par2`, `local_avg_par2`)
- One sentence: faster/slower/comparable? Flag >20% systematic differences as potentially a build config issue.
- Last line: `baseline_local: benchmark/baseline_local.csv`

Be terse. Only return the final summary — no narration.

---

4. Relay the subagent's summary verbatim to the user.

## Notes
- After running, future `/benchmark` runs compare against local performance, not historical DRPM_0L numbers.
- To revert to frozen CSV: set `"baseline_source": "benchmark/baseline.csv"` and `"baseline_column": "DRPM_0L"` in `state.json`.
