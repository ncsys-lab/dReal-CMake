# /benchmark-baseline skill

Re-establish a local baseline by running a stratified sample of benchmarks with the current binary.
Use this periodically (before merging branches, or when the exceptional list grows stale).

## Steps

1. **Confirm with user first:**
   > This will run ~30 benchmarks with the current binary and takes roughly 15-20 minutes. It will update `benchmark/state.json` to use a local baseline instead of the frozen CSV. Proceed?

   Wait for confirmation before continuing.

2. **Tell the user** "Baseline run starting — I'll report back when done."

3. **Spawn a Haiku subagent** to handle everything. Spawn with this prompt (fill in the literal git SHA before spawning):

---
**Subagent prompt** (replace `<SHA>` with the actual short SHA from `git -C /Users/kunalsheth/Documents/new_dreal/dreal4-cmake rev-parse --short HEAD`):

You are re-establishing a local performance baseline for the dReal4 SMT solver project at `/Users/kunalsheth/Documents/new_dreal/dreal4-cmake`. Complete all steps below and return a formatted summary as your final message.

**Step A — Check oom_killer.sh** (required before any SMT solver runs):
```bash
pgrep -f oom_killer.sh || nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
```

**Step B — Create output dir:**
```bash
TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="/Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/results/baseline_<SHA>_${TS}"
mkdir -p "$OUT_DIR"
echo "$OUT_DIR"
```

**Step C — Select stratified sample and run** (run in the **foreground** with a 1200000ms timeout — `run_batch.sh` parallelizes internally; do NOT use `run_in_background` here):
```bash
cd /Users/kunalsheth/Documents/new_dreal/dreal4-cmake
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
```
The command will block until all benchmark jobs complete.

**Step D — Parse results:**
```bash
python3 /Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/parse_results.py "$OUT_DIR"
```

**Step E — Save as local baseline:**
- Copy `$OUT_DIR/summary.csv` to `benchmark/baseline_local.csv`
- Update `benchmark/state.json`: set `"baseline_source": "benchmark/baseline_local.csv"`, `"baseline_column": "wall_time_s"`, and record the SHA `<SHA>` under a `"baseline_sha"` key

```bash
cp "$OUT_DIR/summary.csv" /Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/baseline_local.csv
python3 - <<'PYEOF'
import json, sys
path = "/Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/state.json"
with open(path) as f: state = json.load(f)
state["baseline_source"] = "benchmark/baseline_local.csv"
state["baseline_column"] = "wall_time_s"
state["baseline_sha"] = "<SHA>"
with open(path, "w") as f: json.dump(state, f, indent=2)
print("state.json updated")
PYEOF
```

**Step F — Compute per-family averages** by reading both `benchmark/baseline_local.csv` (local) and `benchmark/baseline.csv` (frozen, column `DRPM_0L`) and comparing average wall times per family (saradc/github/tacas).

**Step G — Return a formatted summary** as your final message:
- Header: `Baseline established from <SHA> — N benchmarks across 3 families`
- Table: one row per family showing frozen avg vs local avg and the ratio
- One sentence: is the local binary faster, slower, or comparable overall? Flag large systematic differences (>20%) as potentially indicating a build configuration difference.
- Last line: `baseline_local: benchmark/baseline_local.csv`

Be terse. Do not narrate your steps — only return the final formatted summary.

---

4. **Relay** the subagent's summary verbatim to the user.

## Notes
- After running `/benchmark-baseline`, future `/benchmark` runs compare against the local binary's own performance, not the historical DRPM_0L numbers.
- To revert to the frozen CSV baseline: set `"baseline_source": "benchmark/baseline.csv"` and `"baseline_column": "DRPM_0L"` in `state.json`.
