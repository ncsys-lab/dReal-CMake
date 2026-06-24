# Benchmarking

Run `/benchmark` after every meaningful code change — the primary regression-detection mechanism.
Run proactively at natural breakpoints even if the user doesn't ask.

---

## Skills

- `/benchmark` — runs ~8-12 family-weighted benchmarks in parallel, spawns a Haiku subagent to
  interpret results, reports back a 2-4 sentence summary with regression/exceptional counts
- `/benchmark-baseline` — runs all 43 odeexpr + ~10 each of the other three families to
  establish a fresh local baseline (use before branch merges or when the exceptional list grows
  stale)

---

## Thresholds

- **Regression:** PAR2 time >1.5× baseline (PAR2 = actual CPU time if solved, 2× timeout = 1200 s
  if TIM/OOM/ERR)
- **Exceptional:** PAR2 time <0.6× baseline
- **Correctness flip** (SAT↔UNSAT): immediate escalation regardless of timing

---

## Benchmark sources

- `~/Documents/new_dreal/ode_expressivity/benchmarks/` — `odeexpr` family (43 self-contained
  `.smt2`, content-addressed via `manifest.json`; each sets its own `:precision`; no ground-truth
  `:status`)
- `~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/` — `github_oct5_` family
- `~/Documents/new_dreal/nraode_to_nra/VNAMSCwI_satoct11/rolled/` — `tacas_c2e2_` family
- `~/Documents/new_dreal/AMS-verification-bundle-of-sticks/saradc/rolled/` — `1mhz_` family

---

## Infrastructure (`benchmark/` directory)

- `baseline.csv` — frozen DRPM_0L reference times for 102 benchmarks (good_benchmarks.csv subset)
- `baseline_odeexpr.csv` — odeexpr-family reference times (`cpu_time_s` column); produced by
  `do_baseline_odeexpr.sh`; authoritative for `aggregate.py` on `odeexpr_*` rows
- `odeexpr.py` — single source of truth for the `odeexpr` family: `ODEEXPR_ROOT`,
  `FAMILY_WEIGHTS`, `family_of`, manifest-based `load_odeexpr_names`/`resolve_odeexpr`, `--all`
  TSV dump
- `state.json` — persistent anomaly/exceptional tracker; updated automatically each run
- `run_batch.sh` — parallel runner: reads TSV from stdin, runs each with `gtime -v -o`,
  `nice -n 1`, `timeout 600`. Env hooks: `DREAL_ARGS` injects per-invocation solver flags;
  `TIMEOUT` overrides the 600 s cap; `DREAL_BINARY` overrides the solver binary. Throttle caps
  at 12 concurrent — count actual solvers with `pgrep -x dreal4`, NOT `pgrep -f gcc_build/dreal4`
  (the latter also matches `gtime`/`nice`/`timeout` wrappers, ~3 per solve)
- `select.py` — picks 8 **family-weighted** random benchmarks + all current anomalies; outputs
  TSV (csv_name TAB filepath). `--family a,b,c` restricts corpus to those families
  (`odeexpr,saradc,github,tacas`); `--all` emits every benchmark of the filtered corpus
  deterministically (no random, no anomalies) — for an A/B over a fixed set
- `do_ab.sh BIN_A BIN_B [jobs_file]` — A/B two solver builds over the **same** jobs (default =
  full ODE family). Runs **sequentially** (never concurrently — overlapping batches starve jobs
  and turn real solves into false wall-clock TIMs) via `run_batch.sh` (`DREAL_BINARY`), parses
  each, and emits a `compare_solvers.py` table. The sanctioned way to compare e.g. committed-HEAD
  vs a working-tree build
- `do_sweep.sh NAME1="flags1" NAME2="flags2" …` — sweeps **one** binary over many flag configs
  on the same jobs, for meta-parameter tuning. Runs **all (config × benchmark) pairs in ONE
  shuffled 12-way pool** so a slow config's long-pole overlaps other configs' fast jobs — full CPU
  use, no idle tail, while still ≤`MAXJOBS` concurrent (per-process CPU-time stays accurate).
  Env: `JOBS` (default `probe_odes.tsv`), `DREAL_BINARY`, `MAXJOBS` (12), `TIMEOUT` (600).
  Emits per-config `summary.csv` + a `compare_solvers.py` table
- `parse_results.py` — parses gtime output + solver stdout into `summary.csv` (primary timing
  column `cpu_time_s` = user+sys; `wall_time_s` kept as reference/TIM backup)
- `aggregate.py` — compares vs baseline on CPU time, flags regressions/exceptional, updates
  `state.json`
- `results/` — per-run output directories (gitignored)

---

## Families and weighting

Four families, classified by name prefix (`odeexpr.family_of`):

- `saradc` — prefix `1mhz_`
- `github` — prefix `github_oct5_`
- `tacas` — prefix `tacas_c2e2_`
- `odeexpr` — prefix `odeexpr_<bench_id>` (**high-priority**; these are NRA-only — no ODEs,
  runs the `IcpSeq → Fixpoint[IbexFwdbwd, Integer]` path)

`FAMILY_WEIGHTS = {odeexpr:6, saradc:3, github:2, tacas:2}` encodes relative importance.
The weight drives weighted-without-replacement selection (odeexpr appears ~3× as often per item)
and a `weighted_overall` PAR2 in the family comparison; odeexpr regressions are tagged
`ODEEXPR`/`ODEEXPR-HIGH` so reports lead with them.

**Note:** `OPTIMIZATION_LOG.md` (§Adopted/§Rejected) is all CAPD/ODE-path tuning and is
**orthogonal to odeexpr** — odeexpr has no ODEs.

---

## Timing

The metric is **CPU time (user+sys)**, not wall clock — the machine is multi-tenant, so wall
clock is noisy. Solver runs under `nice -n 1`. `timeout` stays wall-clock at **600 s** (TIM
detection keys on exit code 124).

---

## Manual invocation

```bash
python3 benchmark/select.py | bash benchmark/run_batch.sh benchmark/results/run_$(git rev-parse --short HEAD)_$(date +%s)
python3 benchmark/parse_results.py <results_dir>
python3 benchmark/aggregate.py <results_dir>
```

Re-baseline the odeexpr family (after the set is regenerated, or to refresh reference times):

```bash
bash benchmark/do_baseline_odeexpr.sh   # runs all 43 at 600 s → benchmark/baseline_odeexpr.csv
```

---

## Cross-solver comparison

`run_batch.sh` honors `DREAL_BINARY`, so any alternate native build can be run over the same jobs:

```bash
DREAL_BINARY=/usr/local/bin/dreal4_cav26 bash benchmark/run_batch.sh <out_dir> /tmp/jobs.tsv
```

`run_dreal3.sh` runs the set through dReal v3.16.12 in Docker (`dreal3:1.1`). **macOS gotcha:**
enforce the timeout *inside* the container (`timeout -s KILL 600 ./dReal`) — a host-side
`timeout` around `docker run` only kills the docker client, leaving the container running in the
VM as a zombie. Timing is the in-container CPU time (bash `time`); SIGKILL exit (137) normalized
to TIM.

`compare_solvers.py LABEL=summary.csv …` joins per-solver summaries by benchmark and reports
solve counts, SAT/UNSAT disagreements, solve-set deltas, and CPU-time speedups on
commonly-solved benchmarks. Frozen reference results: `baseline_odeexpr_cav26.csv`,
`baseline_odeexpr_dreal3.csv`; rendered table: `odeexpr_solver_comparison.txt`.

As of HEAD (arm64, upgraded IBEX/CAPD): identical solve-set + verdicts vs cav26 but ~2–3×
faster; ~6–20× faster than dReal3, which also solves 2 fewer. No SAT/UNSAT disagreements among
the three.

---

## Experimental design for sweeps

For meta-parameter tuning: OFAT probe → interaction check → 123-job confirm. Sweep metric and
soundness-flip rules: `OPTIMIZATION_LOG.md` "2026-06 re-tuning campaign".
