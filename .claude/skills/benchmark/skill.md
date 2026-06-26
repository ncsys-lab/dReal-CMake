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

Then, as the FINAL part of your output, ALWAYS render the per-family PAR2 table from `family_comparison` in `aggregate.json` (the run vs the frozen baseline, `baseline_sha` in the same file). One markdown table, one row per family plus `weighted_overall`, columns: Family | Weight | n | Baseline PAR2 (s) | This run PAR2 (s) | Ratio. Sort families by descending weight (odeexpr, saradc, tacas, github) with `weighted_overall` last. Flag ratio >1.5 as a regression and <0.6 as exceptional. If `family_comparison` is absent (older run), say so in one line instead of inventing numbers.

Be terse. Only return the final summary + the PAR2 table — no narration.

---

3. Relay the subagent's summary verbatim to the user.

## Notes
- Binary at `gcc_build/dreal4` — if missing, ask user to run `./BUILD.sh` first.
- If exceptional list grows large across rounds, suggest `/benchmark-baseline`.

## Operational rules (lessons learned — apply to ALL benchmarking, not just this skill)

These govern any benchmark batch, A/B, or meta-parameter sweep you run by hand (e.g.
`do_sweep.sh`, `do_ab.sh`, `run_batch.sh`), not only the canned scripts above. Canonical
detail: `docs/benchmarking.md`; resource limits also in the project memory.

1. **Cap: never >12 `dreal4` processes at once.** The throttle must count `pgrep -x dreal4`,
   NOT `pgrep -f gcc_build/dreal4` (the `-f` form also matches the `gtime`/`nice`/`timeout`
   wrappers, ~3 per solve, so 12 real solves read as ~39 and the throttle breaks). MAXJOBS=12.
   Run **one pool at a time** — never overlap two sweeps, and start no ad-hoc `dreal4` while a
   pool is live.
2. **Saturate all 12 cores — pool, don't batch.** For a sweep over configs, run ALL
   (config × benchmark) pairs in ONE shuffled 12-way pool (`do_sweep.sh`), never one batch per
   config. Per-config batches drain to their 2–3 long-poles (a k256 thermostat, a stress
   benchmark) while the other cores idle; pooling overlaps a slow config's long-pole with other
   configs' fast jobs. Prefer fewer, larger pooled rounds to minimize inter-round idle.
3. **SIGKILL ⇒ blacklist, never restart.** The machine runs `oom_killer`/`swap_killer` daemons
   that SIGKILL any process over **8 GB RAM** instantly. A solver exit by signal (exit code
   **137** = 128+SIGKILL, or "Killed") is a memory event, not a result: do NOT retry it; append
   it to `benchmark/optsearch/blacklist.txt` and exclude it from all future rounds. Report it as
   excluded, never as TIM/ERR. Filter a jobs file with
   `awk -F'\t' 'FILENAME==ARGV[1]{bl[$0]=1;next} !($1 in bl)' blacklist jobs.tsv` — use the
   `FILENAME==ARGV[1]` form, NOT `NR==FNR` (which mis-handles an empty blacklist and silently
   drops every row → a 0-job no-op sweep). `select.py` also has a static `_is_oom_risk` filter
   for known-huge instances; the blacklist is the dynamic complement.
4. **Timing hygiene: a measured run needs a quiet machine.** Do NOT compile while a timing pool
   runs — a background build (`-j8`, or a CLion auto-build that rebuilds on file save) contends
   for cores and silently inflates wall time / risks false 600 s TIMs. Correctness work (ctest)
   can overlap; baselines / A/Bs / sweeps cannot. CPU time (user+sys), not wall clock.
5. **Compare ratios *within* a round, not absolute CPU across rounds.** Absolute CPU shifts
   ~10–17% between runs from memory-bandwidth contention, so always include a `base`/control
   config in the same pool and report each variant's ratio-vs-base; confirm a winner in a final
   pooled round, not by comparing two separately-run batches.
6. **Report PAR2 excluding both-TIM benchmarks.** Benchmarks that time out on BOTH the control
   and the variant add an identical penalty to each side and can dominate the raw PAR2 sum
   (e.g. 12/50 odeexpr TIM for every config ≈ 92% of the sum), diluting the real effect to near
   zero. Report PAR2 over the subset solvable by *either* arm (≈ CPU on commonly-solved) — that
   is the honest speedup; the raw aggregate undersells a win concentrated in a few benchmarks.
7. **A SAT↔UNSAT verdict flip halts the round** — it is an integration bug or a δ-boundary
   completeness shift, never an accepted "win." A "zero verdict flips" benchmark result does NOT
   clear a correctness-class change; curated unit tests catch sharp cases a corpus can't.
8. **New `.cc` files need a CMake reconfigure** before they're built/run (sources are GLOB'd
   without `CONFIGURE_DEPENDS`); `BUILD.sh`/bare `cmake --build` silently skip them. The suite
   count or solve-set going up is the tell.
