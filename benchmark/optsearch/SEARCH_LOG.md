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
Configs: base, acid s3b∈{2,5,10,20,50}, split-ratio∈{0.55,0.6,0.65}.

### Round 2 results (DONE 2026-06-26) — zero verdict flips across all 9 configs

| config | flags | solved | PAR2 | ratio |
|---|---|---|---|---|
| split55 | --split-ratio 0.55 | 38/50 | 15228 | **0.972** |
| base | (default) | 38/50 | 15663 | 1.000 |
| acid_s3b10 | --acid --s3b 10 | 37/50 | 16105 | 1.028 |
| acid_s3b5 | --acid --s3b 5 | 37/50 | 16189 | 1.034 |
| split65 | --split-ratio 0.65 | 37/50 | 16285 | 1.040 |
| acid_s3b50 | --acid --s3b 50 | 36/50 | 17231 | 1.100 |
| acid_s3b20 | --acid --s3b 20 | 36/50 | 17396 | 1.111 |
| split60 | --split-ratio 0.6 | 36/50 | 17401 | 1.111 |
| acid_s3b2 | --acid --s3b 2 | 36/50 | 17472 | 1.115 |

**Takeaways:**
1. **`split-ratio 0.55` is the robust aggregate winner** — 0.972× in *both* rounds (PAR2 15228
   vs 15229; base 15663 vs 15660), same 38/50 solve count. The pooled-ratio metric is highly
   reproducible. Optimum is ~0.55: 0.6/0.65 degrade (push a benchmark to TIM). ~3% real win.
2. **No `s3b` rescues ACID.** s3b=10 stays the best ACID (1.028×); 2/5/20/50 all worse (and
   −1 to −2 solved). The cs5c_sigmoid TIM persists across s3b → ACID is net-negative on the
   *aggregate* at any shave depth. It remains a strong **per-workload** lever (tanh_decrease),
   just not a family default. s3b drill closed.
3. **Aggregate ceiling ≈ 3%** with the contraction/branching levers — consistent with the prior
   conclusion that odeexpr is bounded by the irreducible gaol-transcendental floor (30–45%).
   The remaining headroom is *node-count reduction that doesn't add per-node transcendental
   evals* — i.e. smarter **variable choice** (smear), the next strategy.

### Round 3 results (DONE 2026-06-26) — fine split scan + split×acid combo, zero flips

| config | flags | solved | PAR2 | ratio |
|---|---|---|---|---|
| split56 | --split-ratio 0.56 | 38/50 | 15187 | **0.969** |
| split55 | --split-ratio 0.55 | 38/50 | 15230 | 0.972 |
| split58 | --split-ratio 0.58 | 38/50 | 15262 | 0.974 |
| base | (default) | 38/50 | 15666 | 1.000 |
| s55acid | --split-ratio 0.55 --acid | 36/50 | 17138 | 1.094 |
| split52 | --split-ratio 0.52 | 36/50 | 17347 | 1.107 |
| split54 | --split-ratio 0.54 | 36/50 | 17468 | 1.115 |

**Takeaways:**
1. **split-ratio optimum is a plateau ~0.55–0.58; 0.56 nominal best (0.969×, 38/50).** Robust
   ~3% win, reproducible (split55 = 15230 a 3rd time). Below 0.55 (0.52/0.54) drops to 36/50
   (benchmark-specific TIM-tipping; the sub-0.55 curve is noisy, the ≥0.55 plateau is stable).
2. `--split-ratio 0.55 --acid` (1.094×) confirms **ACID does not compose at family level** — its
   losses dominate even with the split win. ACID stays a per-workload-only lever.
3. **Best family recipe so far: `--split-ratio 0.56`** (~3.1% PAR2, 38/50, zero flips).

### Smear branching implemented (Phase 2)
`--smear` (`SmearBrancher`, smearsumrel; Jacobian-weighted `Σ_i|J[i][j]|·diam/NC_i`, `Bisectors.md`)
built + gated: Release ctest 662/664 (flaky ITE only), 2 smear unit tests pass (picks the
Jacobian-dominant variable, not the widest; falls back to largest-first cleanly), rounding gate
PASS, copy_lint clean, verdict parity verified on smoke benchmarks. Default-off — `icp_seq`
unchanged when the flag is absent. The System's vars are box-ordered so Jacobian columns map 1:1
to box dims. Sound: variable choice can't change a verdict.

### Round 4 — smear branching (running)
Configs: base, split56 (current best), smear, smear+split0.56.

### Round 4 results (DONE 2026-06-26) — smear is net-negative, zero flips

| config | flags | solved | PAR2 | ratio |
|---|---|---|---|---|
| split56 | --split-ratio 0.56 | 38/50 | 15190 | **0.970** |
| base | (default) | 38/50 | 15667 | 1.000 |
| smear56 | --smear --split-ratio 0.56 | 33/50 | 20413 | 1.303 |
| smear | --smear | 33/50 | 20837 | 1.330 |

**Takeaways:**
1. **Smear branching is net-negative (1.33×, −5 solved).** It loses exactly the slowest
   benchmarks (tanh_decrease J0.6/J1.0, cs2b_dgas, cs5c_sigmoid, kuramoto N4/N5; kuramoto_doe
   +432%) — the per-node Jacobian eval adds transcendental evals that push them over TIM. It
   gained only 1 (tanh.decrease_slope). Even combined with split-0.56 it's 1.30×. Rejected.
2. split-0.56 holds at 0.970× (4th consistent confirm).

## Unifying conclusion (after 4 rounds)

**The dominant cost on odeexpr is the gaol correctly-rounded transcendental evaluations (the
30–45% irreducible floor from `OPTIMIZATION_LOG.md`).** Every lever splits cleanly by whether it
*adds per-node transcendental evals*:

| Lever | Category | Result |
|---|---|---|
| `--split-ratio 0.56` | reorder search, **no extra evals** | **win, ~3%** (the one aggregate win) |
| `--acid` / `--3bcid` | stronger contraction, **+ evals/node** | net-negative on family; **per-workload win** (tanh_decrease) |
| `--smear` | smarter var choice, **+ Jacobian evals/node** | net-negative (−5 solved) |
| `--worklist-fixpoint` | reorder, but catastrophic on hard UNSAT | net-negative (−4 solved) |

→ Levers that add evals lose; only the free reorder (split point) wins. This re-confirms the
prior "transcendental floor" conclusion on today's patched binary, against all four levers.

### Recommended recipe (per-workload, the search's deliverable)
- **Family default: `--split-ratio 0.56`** — ~3.1% PAR2, 38/50, zero flips, reproducible 4×.
- **For sharp single-variable nonlinear Lyapunov problems (tanh_decrease-shaped): add `--acid`**
  — up to **16× faster** (J1.0 174s→11s); but NOT for coupled multi-var systems (kuramoto:
  +165–258%) or as a family default (cs5c TIM).
- Avoid `--smear`, `--3bcid`, `--worklist-fixpoint` on this family.

### Round 5 — constraint ordering (next, the last untested "reorder, no extra evals" lever)
The only winning category is reorder-without-adding-evals. Split-*point* is exhausted; the
remaining one is constraint *order* in the fixpoint (front-load cheap/high-shrinkage constraints
to cut fixpoint iterations → fewer evals). Sound (a fixpoint is order-independent in result, only
in cost). Low confidence per `OPTIMIZATION_LOG.md` D, but it's the principled next try.

### Round 5 results (DONE 2026-06-26) — constraint ordering is a null result, zero flips

| config | flags | solved | PAR2 | ratio |
|---|---|---|---|---|
| split56 | --split-ratio 0.56 | 38/50 | 15185 | **0.969** |
| corder_asc_s56 | --constraint-order asc --split-ratio 0.56 | 38/50 | 15191 | 0.970 |
| corder_asc | --constraint-order asc | 38/50 | 15666 | 1.000 |
| base | (default) | 38/50 | 15668 | 1.000 |
| corder_desc | --constraint-order desc | 37/50 | 16304 | 1.041 |

**Takeaways:** constraint ordering has **no effect** — `asc` is identical to base (1.000×), `desc`
slightly worse (1.041×), and `asc+split0.56` equals split0.56 alone. dReal's worklist already
skips unaffected constraints via input/output bitsets, so it has already captured any ordering
benefit. Even the one "reorder, no extra evals" lever besides the split point yields nothing here.

---

# FINAL RECOMMENDATION (search concluded after 5 rounds)

**Best per-workload flag recipe for the odeexpr (QF_NRA) family:**

1. **Family default: `--split-ratio 0.56`** — ~3.1% PAR2 improvement, 38/50, zero verdict flips,
   reproduced identically across 4 rounds (PAR2 ~15185 vs base ~15667). Free at runtime (changes
   only the split point, adds no evaluations). The plateau 0.55–0.58 is all ~0.97×; 0.56 nominal best.
2. **For sharp single-variable nonlinear Lyapunov problems (`tanh_decrease`-shaped): add `--acid`**
   — up to **16× faster** (`tanh_decrease__J1.0` 174s→11s; `__J0.6` −43%). Do **not** use it on
   coupled multi-variable systems (`kuramoto`: +165–258%) or as a family default (it TIMs
   `cs5c_sigmoid`, net −1 solved, no `s3b` rescues it).

**Why nothing beats ~3%:** odeexpr is bounded by the gaol correctly-rounded transcendental
evaluations (30–45% irreducible floor). Levers cleanly split by whether they add per-node evals:

| Lever | Adds evals/node? | Result |
|---|---|---|
| `--split-ratio 0.56` | no (reorder only) | **WIN ~3%** |
| `--constraint-order` | no (reorder only) | null (asc=base; skip-logic already optimal) |
| `--acid` / `--3bcid` | yes (shaving) | family-negative; **per-workload win** (tanh_decrease) |
| `--smear` | yes (Jacobian) | negative (−5 solved) |
| `--worklist-fixpoint` | reorder, but unstable on hard UNSAT | negative (−4 solved) |

Every eval-adding lever loses to the floor; reorder levers either win small (split point) or do
nothing (constraint order). This re-confirms the prior "transcendental floor" conclusion on
today's patched binary, now tested against **five** levers.

**Deferred / not pursued (with rationale):**
- `CtcNewton`-late (AUDIT D1): predicted-negative — it is eval-adding (Jacobian/gradient), the
  same category as smear/ACID which both lost. Not implemented; the floor model already answers it.
- ACID callback-bearing sub-contractor (AUDIT C1): would sharpen ACID's lemmas but cannot fix its
  family-level deficit (the cs5c TIM is integration-cost, not lemma quality).
- Polytope/LP relaxation (AUDIT D2): requires rebuilding IBEX with an LP backend — a dependency
  change to **escalate to the owner**, not take on autonomously.

All knobs are committed as default-off runtime flags; the recommendation is a per-workload recipe,
so the solver's global defaults are unchanged. Leaderboard: `benchmark/optsearch/leaderboard.csv`.

## Confirmation round (R6) — IMPORTANT correction: split-0.56 dominates ACID

The same-contention confirm (base / split56 / acid, all 50, zero flips) cross-compared split56 vs
acid directly for the first time, and overturns the per-workload ACID recommendation:

| benchmark | base | split56 | acid |
|---|---|---|---|
| tanh_decrease__J1.0 (SAT) | 175 s | **0.00 s** | 11 s |
| tanh_decrease__J0.6 (SAT) | 436 s | **189 s** | 245 s |
| cs5c_sigmoid__decrease (UNSAT) | 573 s | **546 s** | **TIM** |

`split-ratio 0.56` is **faster than ACID on ACID's own best cases** (the tanh_decrease symmetric
Lyapunov problems) *and* avoids the cs5c TIM. Per-benchmark best-of-{base,split56,acid} PAR2 =
15191 = split56 exactly → **ACID wins zero benchmarks; it is strictly dominated.**

**Why:** the off-center split breaks the origin-symmetry of these problems (the user's hypothesis)
far more effectively than shaving — on `tanh_decrease__J1.0` it finds the witness essentially
instantly (175 s → ~0). The ~3% *aggregate* undersells it because ~13 hard benchmarks TIM
regardless; the real effect is **100s-of-seconds → instant on the symmetric SAT Lyapunov family.**

### REVISED FINAL RECOMMENDATION
**`--split-ratio 0.56` — single best config for odeexpr, full stop.** ~3% aggregate PAR2, 38/50,
zero flips, and up to ~17000× on individual symmetric SAT problems (J1.0). No per-workload split
needed: ACID, smear, 3bcid, worklist-fixpoint, constraint-order are all dominated or null. The
deeper win than expected is the symmetry-break, not contraction strength. (ACID remains a correct,
sound, default-off flag — just never the best choice on this family.)

## Both-TIM-excluded speedup — the honest headline (review follow-up)

The ~3% aggregate is **diluted by the 12/50 benchmarks that TIM for every config** — each adds an
identical 1200 s to both sides, ~92% of the raw PAR2 sum. Recomputing PAR2 over only the 38
benchmarks solvable by *either* config (identical to CPU-on-commonly-solved here, since no split
config solves-one-while-TIMing-the-other):

| split ratio | PAR2 (all 50) | **PAR2 excl. both-TIM (38)** | note |
|---|---|---|---|
| 0.40 | 1.078× (−7.8%) | **1.97× (−97%)** | net-negative; knocks a base-solved bench into TIM |
| 0.45 | 1.069× (−6.9%) | **1.86× (−86%)** | net-negative |
| 0.55 | 0.972× (+2.8%) | **0.658× (+34%)** | |
| **0.56** | 0.969× (+3.1%) | **0.619× (+38%)** | best |

**Headline: `--split-ratio 0.56` is ~38% faster on the solvable subset** (not 3%). Concentrated:
87% of the 486 s saved is the two symmetric `tanh_decrease` SAT problems (`__J1.0` 175 s→0 s,
`__J0.6` 436 s→189 s); `kuramoto__N5` (−40%) and `cs5c_sigmoid` (−5%) add the rest; the other ~34
solvable benchmarks are near-instant regardless. So: transformative on the symmetric non-trivial
problems, neutral elsewhere. Cutting *left* of center (0.4/0.45) is net-negative.

## The split point and the box-exploration order are coupled — both matter

0.4 and 0.56 are **not mirror-symmetric in effect** (0.4 hurts, 0.56 helps) even though the domains
are symmetric about the origin. A pure cut-*location* effect would make 0.44/0.56 equivalent — so
the **box-processing order** is the symmetry-breaker, coupled to the ratio:

- `Box::bisect(i, ratio)` → `.first = [lb, lb+ratio·diam]` (size `ratio`); the brancher assigns
  `*left = .first`. So at **ratio > 0.5, `box_left` is the LARGER box**.
- `IcpSeq` inits `stack_left_box_first_ = false` → the **root branch processes `box_left` first**
  (then alternates). At 0.56 that means **the larger sub-box is explored first at the root**.

These two behaviors live in **different files** (`util/box.cc` + `solver/brancher.cc` for the
split; `solver/icp_seq.cc` for the order), so the coupling was documented/co-located via the
`ExploreOrder` doc in `config.h` as the canonical home + cross-referencing comments at all three
sites (review action).

### Confirmation experiment — the coupled partner is ALTERNATION, not "bigger-first"

Added `--explore-order {alternate|larger-first|smaller-first}` to isolate order from ratio, and
tested at a fixed 0.56 cut:

| benchmark | config | verdict | wall |
|---|---|---|---|
| tanh_decrease__J1.0 | 0.50 alternate (base) | delta-sat | 157 s |
| tanh_decrease__J1.0 | **0.56 alternate (default)** | delta-sat | **0.0 s** |
| tanh_decrease__J1.0 | 0.56 larger-first | delta-sat | 423 s |
| tanh_decrease__J1.0 | 0.56 smaller-first | delta-sat | 286 s |
| tanh_decrease__J0.6 | 0.56 larger-first | delta-sat | 330 s |
| tanh_decrease__J0.6 | 0.56 smaller-first | TIM | 500 s |

**Hypothesis overturned.** "Explore the larger box first" is the *worst* option (423 s vs 0.0 s at
the same cut), not the win. What's load-bearing is the **default alternating traversal**, and it
needs the off-center cut too:
- `0.50 + alternate` = 157 s → alternation alone (midpoint cut) doesn't win.
- `0.56 + fixed order` = 286–423 s → off-center cut alone (no alternation) doesn't win.
- `0.56 + alternate` = **0.0 s** → only the *pair* (off-center cut × alternating traversal) wins.

So the coupling is real (the user's instinct), but the partner is the alternation, not bigger-first.
The 0.4-vs-0.56 asymmetry is the *cut direction* under alternation, not evidence of bigger-first.
`--explore-order` defaults to `alternate` (the winner); `larger-first`/`smaller-first` are retained
only as experiment knobs (both measured slower). **Recipe is unchanged: `--split-ratio 0.56`** (its
win already includes the default alternation).
