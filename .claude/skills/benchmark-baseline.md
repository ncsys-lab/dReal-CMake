# /benchmark-baseline skill

Re-establish a local baseline by running a stratified sample of benchmarks with the current binary.
Use this periodically (before merging branches, or when the exceptional list grows stale).

## Confirm with user first

Before running, say:
> This will run ~30 benchmarks with the current binary and takes roughly 15-20 minutes. It will update `benchmark/state.json` to use a local baseline instead of the frozen CSV. Proceed?

Wait for confirmation.

## Steps

1. **Check oom_killer.sh**:
   ```bash
   pgrep -f oom_killer.sh || nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
   ```

2. **Select a stratified sample** — 10 from each benchmark family (SARADC, github_oct5, tacas_c2e2):
   ```bash
   cd /Users/kunalsheth/Documents/new_dreal/dreal4-cmake
   python3 benchmark/select.py --n 30
   ```
   Then manually (or via a small inline Python snippet) ensure coverage across all three families. If needed, supplement with:
   ```python
   import csv, random
   rows = list(csv.reader(open("benchmark/baseline.csv")))[3:]
   names = [r[0] for r in rows if r]
   families = {"saradc": [], "github": [], "tacas": []}
   for n in names:
       if n.startswith("1mhz_"): families["saradc"].append(n)
       elif n.startswith("github_oct5_"): families["github"].append(n)
       elif n.startswith("tacas_c2e2_"): families["tacas"].append(n)
   sample = (random.sample(families["saradc"], min(10, len(families["saradc"]))) +
             random.sample(families["github"],  min(10, len(families["github"])))  +
             random.sample(families["tacas"],   min(10, len(families["tacas"]))))
   ```

3. **Resolve paths and run** — generate TSV (csv_name TAB filepath) for the stratified sample and pipe into `run_batch.sh`:
   ```bash
   SHA=$(git rev-parse --short HEAD)
   TS=$(date +%Y%m%d_%H%M%S)
   OUT_DIR="benchmark/results/baseline_${SHA}_${TS}"
   python3 - <<'PYEOF' | bash benchmark/run_batch.sh "$OUT_DIR"
   import csv, random, os
   BASE = "/Users/kunalsheth/Documents/new_dreal/nraode_to_nra"
   DIRS = {
       "github_oct5_": os.path.join(BASE, "drealgithub_sunoct5", "rolled"),
       "tacas_c2e2_":  os.path.join(BASE, "VNAMSCwI_satoct11", "rolled"),
   }
   SARADC = [os.path.join(BASE, d) for d in ("SARADC_tueoct14", "REB_SAR_k1_dec9")]
   rows = list(csv.reader(open("benchmark/baseline.csv")))[3:]
   names = [r[0] for r in rows if r and r[0].strip()]
   fams = {"saradc": [], "github": [], "tacas": []}
   for n in names:
       if n.startswith("1mhz_"): fams["saradc"].append(n)
       elif n.startswith("github_oct5_"): fams["github"].append(n)
       elif n.startswith("tacas_c2e2_"): fams["tacas"].append(n)
   sample = (random.sample(fams["saradc"], min(10, len(fams["saradc"]))) +
             random.sample(fams["github"],  min(10, len(fams["github"])))  +
             random.sample(fams["tacas"],   min(10, len(fams["tacas"]))))
   for n in sample:
       if n.startswith("1mhz_"):
           p = next((os.path.join(d, n) for d in SARADC if os.path.exists(os.path.join(d, n))), None)
       else:
           pfx = "github_oct5_" if n.startswith("github_oct5_") else "tacas_c2e2_"
           p = os.path.join(DIRS[pfx], n[len(pfx):])
           p = p if os.path.exists(p) else None
       if p: print(f"{n}\t{p}")
   PYEOF
   python3 benchmark/parse_results.py "$OUT_DIR"
   ```

4. **Save as local baseline**:
   - The `summary.csv` from this run becomes `benchmark/baseline_local.csv`
   - Update `state.json`: set `"baseline_source": "benchmark/baseline_local.csv"` and `"baseline_column": "wall_time_s"` (since local CSV uses a single timing column)
   - Note the git SHA used for this baseline run in `state.json`

5. **Report** average timing per family vs the frozen CSV baseline, and whether the local binary is faster/slower overall. If there are large systematic differences, flag them — it may indicate a build configuration difference.

## Notes
- After running `/benchmark-baseline`, future `/benchmark` runs compare against the local binary's own performance, not the historical DRPM_0L numbers.
- To revert to the frozen CSV baseline: set `"baseline_source": "benchmark/baseline.csv"` and `"baseline_column": "DRPM_0L"` in `state.json`.
