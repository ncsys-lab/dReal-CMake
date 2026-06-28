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

## Adopted as the GLOBAL default (0.56 + alternate) — owner decision + accepted trade-off

`kDefaultSplitRatio` 0.5 → 0.56 (commit a0802e47a); default brancher now cuts at the constant,
`--split-ratio` overrides. `alternate` was already the default exploration order.

**Cross-family A/B before adopting** (old-0.5 vs new-0.56, 46-benchmark sample = all saradc +
~25% of github/tacas; the 0.56 win is already confirmed on odeexpr):

- **No SAT↔UNSAT disagreements** (sound).
- **Neutral on the ODE families' commonly-solved benchmarks**: aggregate 0.99×, median 1.00×
  (range 0.62–1.17×) — the symmetry-break benefit does **not** transfer to saradc/github/tacas.
- **One completeness regression in the sample**: `tacas_c2e2_0hz_k15_..._ramp` (SAT) 10.8s → **TIM**.
  The sample was ~25% of github/tacas, so the full corpus likely holds a few more such regressions
  (exact count un-quantified — a full baseline would settle it).

**Decision (owner):** keep 0.56 as the global default — odeexpr out-of-the-box speed is worth the
small, sound (completeness-only, never false-`unsat`) cost on the ODE families. Override per-workload
with `--split-ratio 0.5` if an ODE-family run needs the old behavior. Recorded so the default's known
cost isn't later mistaken for free (cf. the hull-grid owner-accepted-completeness-tradeoff precedent).

---

# Why branching matters here, and why no *order* is robust (2026-06, follow-up investigation)

The 0.56-plus-"alternation" default was filed as a smell: a load-bearing solver decision nobody
could rationalize. This follow-up instrumented the mechanism, tested the obvious principled
replacement, and reached an honest negative-on-the-strategy / positive-on-the-understanding result.

## The win is a search-TREE-SIZE collapse, at an off-center witness

`tanh_decrease__{J1.0,J0.6}` are single-`CheckSat` solves, so the existing `IcpStat::num_branch_`
counter already gives nodes-to-witness (no instrumentation code needed). Node counts (CPU tracks
them linearly at ~125k branches/s — it is tree **size**, not per-node cost):

| benchmark | config | nodes | CPU |
|---|---|---|---|
| J1.0 | 0.50 + alternate | 22,803,572 | 181 s |
| J1.0 | **0.56 + alternate** | **69** | **0.00 s** |
| J1.0 | 0.56 + larger-first | 52,914,427 | 432 s |
| J1.0 | 0.56 + smaller-first | 27,528,292 | 224 s |
| J0.6 | 0.50 + alternate | 55,227,828 | 442 s |
| J0.6 | 0.56 + alternate | 24,732,081 | 198 s |

A **~330,000× node collapse** on J1.0. The witness is dramatically off-center (`x_1≈0.76,
y_1≈-0.83` in a `(-1,1)` box). (These corroborate the wall-clock table above.)

## Mechanism: a degenerate (flat, ∇=0) feasibility landscape at the symmetric center

`tanh_decrease` is pure QF_NRA: 9 vars on origin-symmetric `(-1,1)` boxes, two `cse` definition
equalities, and **one** hard constraint — the Lyapunov residual `BigExpr > 3/2000`. At the
symmetric origin every term of `BigExpr` is a product of factors that each vanish at 0, so
`BigExpr(0)=0` **and** `∇BigExpr(0)=0`: the origin is a **critical point**, and the geometric
center *violates* the constraint (`0 > 0.0015` is false). So the feasible region is off-center,
and near the center — where the search starts and dwells — the constraint is **flat**: the
contractor cannot prune (no gradient to propagate) and the search must bisect many dimensions to
isolate the off-center solution. That is *why* branching matters so much here, and why the
"alternation" is not really magic: the off-center cut is a **gradient-free symmetry-break** that
escapes the flat basin by brute geometry.

## Feasibility-guided ordering — implemented, then REFUTED

The obvious principled replacement: dive into the child whose center is closer to feasibility
(`ExploreOrder::kFeasibilityGuided`, a `CenterInfeasibility` point-evaluator). It **fails**:

- With all constraints, the `cse` *equalities* dominate the score — a point box is never on an
  equality surface, so each contributes an O(1) residual that swamps the hard inequality's ~0.0015
  signal. Direct test: `J1.0 @0.56` = **120 s TIM**.
- Inequality-only (skip equalities) gave a clean per-node signal but still did not converge: the
  per-branch trace showed scores **tie** on the three `J` dimensions near the origin (the `∇=0`
  flatness, observed directly) and the center residual bouncing far from a delta-box.
- **Root cause:** gradient-following needs a gradient; there is none at the symmetric center. A
  gradient-free symmetry-break is exactly what beats it. (Reverted; the harness that would have
  given inequality-only an end-to-end *timing* had a bash-3.2 associative-array bug that silently
  passed empty paths — the valid evidence is the 120 s with-equalities TIM and the non-converging
  trace.)

## The magic is brittle, and no traversal scheme robustly wins

`0.56 + alternate` is **69 nodes on J1.0** but **24.7M nodes / 198 s on J0.6** — a lucky lottery
ticket, not a robust strategy (the smell was real). The "alternation" itself was a single member
flag toggled on *every branch event in DFS order* — path-dependent, the genuinely-arbitrary part.
Replaced it with **depth-parity** (explore `box_left` first at even tree depth — path-independent,
"alternate per level") and A/B'd vs the legacy global-toggle:

| benchmark | split | global-toggle | depth-parity |
|---|---|---|---|
| J1.0 | 0.56 (default) | 69 / 0.00 s | 69 / 0.00 s (tie) |
| J1.0 | 0.50 | 22.8M / 178 s | 3.65M / 27.8 s (6.4× fewer nodes — but still ≫ 0.56's 69) |
| J0.6 | 0.56 (default) | 24.7M / 194 s | 32.1M / 231 s (+30% nodes) |
| J0.6 | 0.50 | TIM | TIM |

Full odeexpr-family A/B (both at default 0.56): **zero verdict flips, 38/50 solved by each, neither
solves anything the other doesn't**; median per-benchmark ratio 1.00×, the one real difference being
J0.6 (+30% nodes under depth-parity). So depth-parity is more *intelligible* but perf-neutral-to-
slightly-worse at the default; **not adopted** (it regresses J0.6, and its only "gain" — 6.4× on
J1.0@0.50 — is inside the 0.50 regime, which 0.56 strictly dominates on both benchmarks).

## Conclusions

1. **0.56 is a principled off-center symmetry-break, not a magic number.** It is load-bearing:
   `--split-ratio 0.5` regresses both transformative benchmarks (J1.0 0.00s→27.8s, J0.6 solve→TIM)
   under *either* traversal scheme. Keep it.
2. **Branching *order* is a high-variance lever with no robust winner.** Feasibility-guided is
   worse; depth-parity and the legacy global-toggle each win some configs and lose others. Chasing
   a better deterministic order is not fruitful — kept the proven global-toggle, no code change.
3. **The genuinely robust fix is local-search seeding, not a branching tweak.** Finding an
   off-center solution in a flat landscape is what `nlopt` is for: seed the ICP search with a
   locally-optimized candidate point and verify a delta-box around it. Scoped in
   `docs/seeding.md`; dReal already links nlopt (`src/dreal/optimization/`).

# Seed-and-verify (`--seed-local`): built and benchmarked (2026-06, follow-up)

This is the fix item 3 points to, implemented (`src/dreal/solver/seed/seed.{h,cc}`, hooked in
`IcpSeq::CheckSat`) and benchmarked across all configs.

## Architecture (soundness/completeness free)

A speculative pre-pass, default off, gated to pure-relational (NRA) theory calls (`AllRelational`
— skips `forall`/ODE): **propose** candidate points → **pin** a small SOUND box around each
(`make_sound_interval` endpoints ∩ root box) → push onto the ICP stack to be explored FIRST →
**verify** by the *unchanged* prune+`EvaluateBox` loop. The root box stays on the stack, so it is
the cache/recompute carve-out shape, NOT a fallback: a poor candidate cannot cause a false
delta-sat (EvaluateBox is the sole arbiter) and no subspace is dropped. *(COMPLETENESS-only — see
the soundness mandate.)* Proven by a test→RED→fix→GREEN cycle: a "trust-the-seed-without-verify"
bypass flips the UNSAT guards to false delta-sat (RED); the verify-only path is GREEN
(`test/dreal/solver/seed/test/seed_test.cc`, 6/6 pass at the time; since expanded into a
full adversarial unit suite).

Two proposers (`--seed-method`): **`lhs`** (Latin-hypercube sampling — gradient-free, no
flat-center vulnerability; the default per the "why not just sampling?" pivot — grid is rejected,
k^d in 9-D) and **`nlopt`** (multi-start COBYLA local optimization). The budget is `--seed-samples`.

## nlopt needed three real fixes (a fair test, each a finding about the instance structure)

The odeexpr boxes mix bounded primaries (x/y/J ∈ (−1,1)) with **equality-defined CSE auxiliaries
that carry no bound**. nlopt failed on even the easy J1.0 until all three were fixed:
1. **Unbounded CSE dims** → pin only finite dims, leave CSE dims for the contractor's HC4 to derive
   (this also made LHS fire at all).
2. **CSE equalities sabotage COBYLA** (init-0 cse with `cse=2x−2y` *pulls toward the origin* = the
   infeasible center; and an unconstrained ±inf dim wanders to NaN → "NULL args"). Fix: substitute
   the CSE defs out (`DerivedSubstitution`, chain-resolved) and optimize a **sub-box of only the
   bounded primaries**.
3. **`>` stored as `¬(≤)`** → `ConstraintViolation` returned 0 → empty objective → nlopt aborts.
   Fix: NNF-normalize (`Nnfizer::Convert(f, true)`). The shared `ConstraintViolation` was factored
   out of the forall refiner (`nlopt_optimizer.cc`) and reused.

## Spot-check: budget to crack the transformative instances (0.50 midpoint regime, which alone TIMs)

| instance | feasible region | LHS budget | nlopt budget |
|---|---|---|---|
| `tanh_decrease__J1.0` | fat | 256 | **8** (nlopt-1 center-only FAILS) |
| `tanh_decrease__J0.6` | tiny (f ≈ 1e-4) | **65 536** | **64** |

- **Guided ≫ blind for tiny regions:** nlopt cracks J0.6 with **64** starts where LHS needs
  **64 000** (~1000×) — each COBYLA start descends to feasibility instead of relying on a random hit.
- **The flat center is real — for nlopt, not LHS:** `nlopt-1` (single COBYLA start from the box
  center) TIMs on J1.0 — its `rhobeg` simplex does not escape the ∇=0 center from dead-center;
  `nlopt-8` cracks it because the off-center LHS starts dodge the flat basin. LHS is immune by
  construction.

## Corpus A/B (odeexpr, 50 jobs, 300 s cap; seeding fires). Honest PAR2 over the 40 ever-solved:

| config | solved | PAR2 | SAT↔UNSAT flips |
|---|---|---|---|
| `base056` (0.56 magic, default) | 37 | 51.0 | — |
| `mid050` (0.50, no seed) | 36 | 66.7 | 0 |
| `lhs050_256` | 35 | 77.3 | 0 |
| `lhs050_64k` | 37 | 47.3 | 0 |
| `lhs056_64k` | 36 | 61.4 | 0 |
| `nlopt050_8` | 38 | 32.3 | 0 |
| **`nlopt050_64`** | **39** | **17.3** | **0** |

**`nlopt050_64` dominates**: +2 solved over the 0.56 magic (it cracks `tanh_decrease__xwin1.5` and
`__xwin2.0`, two off-center SAT instances `base056` *times out* on, 0.02 s each), 3× lower PAR2,
**zero soundness flips**, and **no overhead regressions**. It beats the magic *at the magic's own
job* while removing the need for it.

**Honest nuance — the methods are complementary, not strictly ordered:** `nlopt050_64` is not a
strict superset. One instance (`odeexpr_tanh.decrease_d_i__tau0.0015`) is solved *only* by the
large-N LHS configs (`lhs050_64k`/`lhs056_64k`) — its feasible region is one a 64-start COBYLA
misses but 64 000 blind samples hit. So the union (guided ∪ large-blind) would solve 40/40; for a
*single* config, `nlopt050_64` is the best (39/40, lowest PAR2, no regressions).

**The LHS budget dilemma (why guided wins):** blind sampling is caught between coverage and
overhead. `lhs050_256` misses the tiny J0.6-class regions; `lhs050_64k` cracks them but its 64 000
pushed boxes genuinely **regress easy instances to TIM** (e.g. `aim_poly_vs_poly2__N2/N4`, base
0.0 s → 299.9 s real CPU, exit 124 — confirmed compute, not contention). nlopt sidesteps the
dilemma: ~64 guided candidates give both coverage (cracks J0.6) and low overhead (no regressions).

## Conclusions

1. **Seed-and-verify is the principled replacement the branching investigation pointed to** —
   it makes the solver robust to the split ratio (cracks J1.0/J0.6 at the naive 0.50 midpoint that
   otherwise TIMs) and, as `nlopt050_64`, **beats the 0.56 magic outright** (+2 solved, 3× PAR2).
2. **LHS validates the idea and the "why not sampling?" intuition** — blind sampling really does
   crack these off-center instances, gradient-free and flat-center-immune — but it is *dominated*
   by guided search because of the coverage-vs-overhead budget dilemma.
3. **nlopt is the winner once its 3 structural bugs are fixed**, and the win is understood: guided
   descent needs ~1000× fewer candidates than blind sampling on tiny regions, so it gets coverage
   without the overhead that makes large-N LHS regress.
4. Cross-family (github/tacas/saradc, ODE): seeding is gated OFF (`AllRelational` false — verified
   0 `[seed-nlopt]` firings on github/tacas/saradc representatives, because the ODE/integral
   constraints are in every BMC theory call). So nlopt does NOT interact with the ODE families.

## Adopted as the default (2026-06) — and the cross-family A/B that justified it

`--seed-local` is now ON by default with `--seed-method nlopt --seed-samples 64`, `--split-ratio`
back to **0.5**; the 0.56 magic, the `--explore-order` alternate/larger/smaller-first machinery, and
the per-level alternation toggle were removed (sequential exploration is now a fixed per-solve
order). Full A/B, current default (`cur` = 0.56) vs proposed new default (`newdef` = 0.5 + nlopt-64):

| corpus | cur solved | new solved | cur PAR2 | new PAR2 | flips |
|---|---|---|---|---|---|
| odeexpr (50, NRA) | 37 | **39** | 51.0 | **17.3** | 0 |
| github+tacas+saradc (119, ODE) | 113 | **116** | 69.2 | **55.1** | 0 |

The new default is **strictly better on both** (+2 NRA, +3 ODE; zero SAT↔UNSAT flips; no losses).
Tellingly, the 3 cross-family gains (`tacas_c2e2_k12/k15_ramp` TIM→9/11 s, `github prostate_p10`
TIM→42 s) are instances where **seeding never fires** — they are recovered purely by reverting
0.56→0.5, i.e. the old magic had been *hurting* the ODE families (the regression its own config.h
comment admitted). So removing it is a win independent of seeding.

**Authoritative confirmation on the actual built binaries** (old-default binary vs new-default
binary, `do_ab.sh` over the combined 169-job odeexpr+ODE corpus — this captures the fixed-order
change too, which the flag-based A/B above could not): old **151 solved / PAR2 99.1**, new **157 /
77.9**, **+6 solved, 0 losses, 0 SAT↔UNSAT flips**. The 6 gains: `xwin1.5`, `xwin2.0` (odeexpr,
seeding) + `github prostate_p10`/`battery_double-sat`, `tacas k12/k15_ramp` (ODE, the 0.5 revert;
`battery` newly picked up by the fixed order). The fixed-order replacement of the alternation toggle
is otherwise time-neutral on the ODE families (per-instance times match to <1%).

# Interaction with `DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS` (2026-06, follow-up)

Question: does flipping the compile-time `DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS`
(`src/dreal/version.h`, default `true`) to `false` interact with seeding? When `false`, the SAT
solver may return **under-constrained** theory models (fewer theory literals asserted per check;
`context_impl.cc:214–265` then re-checks them through the `recent_under_constrained_deltasat`
exponential-backoff loop). The hypothesis worth testing: under-constrained checks could omit the
ODE/integral atoms, letting `AllRelational` pass and seeding fire on the ODE families where it is
otherwise always gated off.

**Method.** Built two binaries from the one macro (`/tmp/dreal4_FULLtrue` `daa7c2a9`,
`/tmp/dreal4_FULLfalse` `b941c231`; gcc_build restored to FULL=true and md5-verified). Clean **2×2**
— {FULL=true, FULL=false} × {seed-on, seed-off=`--seed-local false`} — over all 169 jobs, 600 s cap,
12-way pool (`benchmark/results/sweep_20260627_{105211,121113}`). PAR2 over the 157 ever-solved:

| | seed-on | seed-off | seeding Δ |
|---|---|---|---|
| **FULL=true** (current default) | **157** / 40.8 s | 153 / 73.6 s | **+4 solved** |
| **FULL=false** | 151 / 136.4 s | 147 / 170.0 s | **+4 solved** |
| FULL Δ (seed-on) | −6 solved, 3.34× PAR2 | | |

Three findings (0 SAT↔UNSAT flips anywhere — soundness intact across all four corners):

1. **No interaction — the knobs are additive.** Seeding delivers the *identical* +4 solved in both
   FULL columns, the *same 4 instances* (`tanh_decrease__J0.6/xwin1.5/xwin2.0`, `cs2b_dgas__decrease`;
   plus J1.0 392 s→0.02 s). Seeding's benefit is fully robust to the FULL setting.
2. **The gate-bypass hypothesis is REFUTED — seeding does not leak onto the ODE families under
   FULL=false.** Programmatic check (`f_seedon` vs `f_seedoff` verdicts): the 4 jobs where seeding
   changes the verdict are *all* odeexpr NRA; **0** github/tacas/saradc verdicts differ. Under-
   constrained BMC theory checks still carry their integral/forall atoms, so `AllRelational` stays
   false there (matches a 0-firing `[seed-nlopt]` probe on all three families).
3. **FULL=false is a regression in its own right** (−6 solved, 3.3× PAR2), orthogonal to seeding —
   and the effect is on the **SAT/UNSAT axis, not the family axis**. Split by true status across all
   families: ALL **UNSAT** (58) PAR2 1831→1299 s (FULL=false **−29%**, 0 solve change); ALL **SAT**
   (99) PAR2 4572→20112 s, solved 99→**93**. Under-constrained models find a refuting conflict
   faster (less to contradict) but force SAT models through the re-check backoff loop. The UNSAT
   speedup is real and concentrated in **saradc** (13 UNSAT, −312 s) and **github** (17 UNSAT,
   −204 s) — those two families simply *have* the most non-trivial UNSAT jobs (tacas has 2, odeexpr's
   UNSAT are all sub-second), which is why the help looks family-specific. But SAT outnumbers UNSAT
   99:58, so even within saradc (SAT +3118 vs UNSAT −312) and github (SAT +4073 vs UNSAT −204) the
   family net is negative. FULL=false would only win if it could be restricted to UNSAT checks, which
   is not knowable in advance.

**Verdict:** keep `FULL_CONSTRAINTS=true` and seeding on — the best of the four corners. Seeding
earns its +4 independent of this experimental knob; FULL=false not adopted.
