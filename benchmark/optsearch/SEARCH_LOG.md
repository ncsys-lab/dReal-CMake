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
(`--worklist-fixpoint`). `base=` doubles as the behavior-neutral reference (cross-check its
verdicts vs cav26's 43 overlapping rows: expect exact match → default path unchanged).

_Results + decision to be appended on completion._
