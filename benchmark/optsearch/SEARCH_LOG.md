# odeexpr config search log

Autonomous search for the best **per-workload flag recipe** for the 50-benchmark `odeexpr`
(QF_NRA) family. Objective: minimize PAR2 of CPU time (user+sys) at `timeout 600`
(PAR2 = CPU if solved, else 1200) with **zero SAT↔UNSAT verdict flips** vs the `base`
(current default) / `baseline_odeexpr_cav26.csv` reference. Winner is a flag combo, not a new
global default — other families keep today's default. Plan:
`~/.claude/plans/i-want-to-optimize-sprightly-ripple.md`.

Soundness framing (project CLAUDE.md): every lever here is a **COMPLETENESS** lever — looser/
absent contraction or a different split risks missed refutation (false `delta-sat`), never a
false `unsat`. A verdict flip is an integration bug or a δ-boundary shift, never an accepted win.

## Implemented knobs (runtime flags on one binary; default-off, behavior-neutral)

| Flag | Effect | Where |
|---|---|---|
| `--split-ratio r` (0<r<1, default 0.5) | branching cut at `lb + r*diam` instead of midpoint | `Box::bisect` ratio param + `BranchLargestFirstWithRatio` + brancher lambda in `dreal_main.cc` |
| `--acid` | ACID adaptive-3BCID shaving over HC4 (`CtcAcid(sys, CtcHC4(sys))`) | new `contractor_ibex_acid.{h,cc}`, wired in `theory_solver.cc` beside polytope |
| `--3bcid` | fixed-param 3BCID shaving (`Ctc3BCid(CtcHC4(sys))`) | same cell |
| `--s3b N` (default 10) | ACID/3BCID shave depth | config |
| `--acid-ct-ratio R` (default 0.002) | ACID adaptive-stop threshold | config |
| `--worklist-fixpoint` | (pre-existing) re-tested as a stale prior | — |

Gates passed before searching: `lint.py` clean; Release ctest 660/662 (only the documented
flaky `IfThenElseEliminatorTest` pair); `rounding_debug_gate.sh` PASS (validates ACID Prune's
`DREAL_ASSERT_ROUNDING(FE_UPWARD)`); `copy_lint.sh` PASS. Unit tests: `BoxTest.BisectRealWithRatio`,
4× `ContractorIbexAcidTest` (sound on SAT, refutes UNSAT, ACID ⊆ HC4, 3BCID). `--split-ratio 0.5`
proven byte-identical to default on a sample (brancher wiring correct).

## Harness

- `benchmark/optsearch/odeexpr_all.tsv` — the 50 jobs (`select.py --family odeexpr --all`).
- `benchmark/optsearch/record_round.py <sweep_out>` — ingests a `do_sweep.sh` round's per-config
  `summary.csv`, computes solved/PAR2/ratio-vs-base, flags SAT↔UNSAT flips, harvests SIGKILL/OOM
  (exit 137) into `blacklist.txt`, appends `leaderboard.csv`, prints a ranked table.
- Jobs filter (drop blacklisted, robust to empty blacklist):
  `awk -F'\t' 'FILENAME==ARGV[1]{bl[$0]=1;next} !($1 in bl)' blacklist.txt odeexpr_all.tsv`.
- One pooled `do_sweep.sh` per round (≤12 procs; saturates cores). See
  [[benchmark-resource-constraints]].

## Round 1 — OFAT screen (IN PROGRESS, launched 2026-06-25)

Pooled sweep, 7 configs × 50 = 350 runs, 600 s cap:
`base`, `acid`, `3bcid`, `split45` (0.45), `split40` (0.4), `split55` (0.55), `wlfp`
(`--worklist-fixpoint`). `base=` doubles as the behavior-neutral reference.

### Round 1 results (DONE 2026-06-26) — zero verdict flips across all 7 configs

| config | flags | solved | PAR2 | ratio | 
|---|---|---|---|---|
| split55 | --split-ratio 0.55 | 38/50 | 15229 | **0.972** |
| base | (default) | 38/50 | 15660 | 1.000 |
| acid | --acid | 37/50 | 16103 | 1.028 |
| 3bcid | --3bcid | 37/50 | 16241 | 1.037 |
| split45 | --split-ratio 0.45 | 37/50 | 16744 | 1.069 |
| split40 | --split-ratio 0.4 | 37/50 | 16886 | 1.078 |
| wlfp | --worklist-fixpoint | 34/50 | 19242 | 1.229 |

**Behavior-neutral CONFIRMED:** base vs `baseline_odeexpr_cav26.csv` (43 overlap) — 30 both-solved,
**0 SAT↔UNSAT flips**, base newly solves 1 cav26-TIM (faster binary). Default path unchanged.

**Takeaways:**
1. No config clears the ≥10% adoption bar in aggregate. split55 marginally best (~3%, same
   solve count — near noise). Off-center signal weakly favors >0.5 (0.45/0.4 both *worse*).
2. **ACID is strongly bimodal — the key finding, hidden by the 1.028× aggregate.** Per-benchmark:
   - Big WINS where shaving breaks sharp nonlinear constraints: `tanh_decrease__J1.0`
     174s→**11s (−94%)**, `tanh_decrease__J0.6` 429s→244s (−43%).
   - Big LOSSES on coupled many-var systems: `kuramoto__N5` 89s→237s (+165%), `__N4` +258%,
     `cs4_equivalence__decrease` +216%; and `cs5c_sigmoid__decrease` base-solved → **ACID TIM**.
   - PAR2 decomposition: the single cs5c TIM (~+1100 PAR2) dominates ACID's net loss; the tanh
     wins (~−350) are real. So ACID is a **per-workload** lever, and an s3b that avoids the cs5c
     TIM could flip ACID net-positive while keeping the tanh win.
3. `--worklist-fixpoint` net-negative on odeexpr (1.229×, −4 solved) — the stale prior holds on
   today's patched binary too. **Re-tested (per "don't disqualify from the log") and rejected.**

### Round 2 — drill s3b + split-ratio (next)
Hypothesis: a different ACID `s3b` keeps the tanh win without TIMing cs5c → ACID net-positive.
Configs: base, acid s3b∈{2,5,10,20,50}, split-ratio∈{0.55,0.6,0.65}. Track the tanh_decrease /
kuramoto / cs5c benchmarks specifically (the bimodal pair) as the s3b tradeoff curve.

_Results to be appended._
