# odeexpr_v2 `forall/` n2 intractability — diagnosis worklog

**Date:** 2026-07-07 · **Binary:** `gcc_build/dreal4` (Jul 6 build) · **Files:**
`~/Documents/new_dreal/ode_expressivity_energy/benchmarks/forall/` (nbody + spring, n2 affine).
Raw logs: session scratchpad `…/exp/`.

## TL;DR

The six new n2 files (nbody scale/shear/affine/rotation `29f8c5ed 25f04758 080cd7cf 6cd7f052`,
spring shear/rotation `1ff9a7e5 e6c80793`) are **quantifier-free NRA whole-box UNSAT refutations**
(the `∀x. descent` claim is negated → search for a counterexample state; verdict `unsat` = property
holds = refute the *entire* box, the exponential ICP direction). They time out because the
**affine-transformed descent expression contracts so poorly under ICP that the tractable-dimension
threshold collapses** from ≥10 free vars (plain 2-body) to ~6, while the problems carry 11–18.
It is a **COMPLETENESS limitation** (missed refutation within budget on a true-`unsat` goal —
never a false `unsat`; no soundness issue).

**The singularity is a red herring** — excluded by the guard (see §Correction). **No existing NRA
flag helps.** The cause is not reducible to any single removable feature.

## What was ruled OUT (each by a controlled experiment)

| candidate | test | result | verdict |
|---|---|---|---|
| **Singularity** `pow(‖Δp‖²,−3/2)` | guard forces `sx·wx=1,sy·wy=1` ⇒ transformed kernel = original, and L1 guard `|Δpx|+|Δpy|≥3/2` keeps original ≥0 | singular region unreachable on any counterexample | **not the cause** |
| **NRA flags** | 11-config sweep on `29f8c5ed`: `--polytope`,`--acid`,`--smear{sum,sumrel}`,`--local-optimization`, all combos | all timeout 60 s | **no remedy** |
| **CSE aux inflation** | flatten all 22 `cse_*` aux reals inline → 14 real vars only | still timeout 90 s | **not the cause** |
| **Affine-param search dims** | pin `sx,wx,sy,wy` to identity *on* the guard manifold | still timeout 120 s | **not (alone) the cause** |
| **One specific inequality** | split consequent → `d1`-only and `d2`-only | both timeout 90 s | **not one inequality** |
| **Bisection depth to δ** | δ = 0.05, 0.1, 0.5, 1.0 on the 10-free case | all timeout 60 s | **not δ-bound** — contraction failure |

## What IS the cause — dimensionality × poor contraction

Reducing the free-variable count is the *only* thing that rescues it (identity params + extra pins):

| free vars | config | result |
|---|---|---|
| 4 | + x2 & masses pinned | unsat 2.75 s |
| **6** | + x2 pinned | **unsat 2.81 s** |
| 7 / 8 / 9 | partial x2 pin | timeout |
| 10 | identity params only | timeout |
| 10 (plain 2-body, **no affine**, `648deddd`) | — | **unsat 3.33 s** |
| n1 (≤8 vars, one body pair) | — | unsat 0.1 s |

Sharp cliff at **6→7 free vars** for the affine expression. Yet the *plain* 2-body descent
(`648deddd`, no affine transform) solves at 10 vars in 3.3 s, and n1 solves at 8. So it is **not raw
variable count** — the **affine transform specifically wrecks interval contraction** (the dependency
problem: state and param variables recur across the gravitational kernel and its multiplying factors;
the second, transformed Lyapunov condition compounds it), dropping the ICP-tractable dimension below
the problem's actual 11–18. Poor contraction ⇒ near-blind bisection ⇒ cost exponential in the coupled
free-var count. Spring behaves identically (`sp_pin_all` with all 3 params pinned barely finishes at
58 s; `sp_pin_sh` with 2 free times out).

**Boundary of isolation (honest):** the wall is the *aggregate* of dimensionality and poor
contraction — no single feature removes it. Flattening CSE, pinning params, dropping either
inequality, and coarsening δ each failed individually; only cutting free vars below ~6 worked.

## Why the contraction is poor — measured (the dependency problem)

Not the pole, not the guard. Within a bounded-kernel branch (Δp ∈ [0.75,8]², so the kernel is
finite) the **true** range of `d1` is `[−493, −51]` — robustly negative — but its **natural
interval extension is `[−941, +941]`** (4.3× too wide, straddling 0). HC4 therefore cannot
certify `d1 ≤ −1e−5` on any usefully-large box → no pruning → bisection, exponential in the
free-var count. Two dependency-problem sources, both making the enclosure straddle 0:

| source | as written | interval | tight value | rewritable? |
|---|---|---|---|---|
| friction | `2·v·(−4·v)` (product of two `v` occurrences) | `[−128,+128]` | `−8v² = [−128,0]` | **yes** → `(pow v 2)` |
| gravity | `v·k·dp`, `k=(dpx²+dpy²)^{−3/2}` (`dp` inside kernel *and* as cofactor) | `[−107,+107]` | — | **no** (irreducible occurrence) |

Rewriting all 8 friction terms to explicit squares **still walls** — friction-fixed enclosure is
`[−941, +429]`, hi > 0, because the gravity kernel-cofactor dependency remains. Tests confirming
the pole/guard are *not* the bottleneck: an explicit box-representable `dpx²+dpy² ≥ 9/8` bound,
and a SAT-core box-exclusion guard `(dpx≤−ε ∨ ε<dpx) ∧ (dpy≤−ε ∨ ε<dpy)` (which bounds the kernel
per branch), **both wall** at every ε; and `id_x2` (pole present, x2 pinned) *solves*.

**Contractor levers — traced (answers "is it IBEX / can we swap the algorithm"): YES an IBEX
capability gap, and the fix is not currently available.**

- dReal's `--polytope` = `ibex::LinearizerXTaylor(RELAX, RANDOM_OPP, HANSEN)` + `ibex::CtcPolytopeHull`
  over SoPlex (`src/dreal/contractor/contractor_ibex_polytope.cc:108-111`). This is an
  **interval-Jacobian** linear relaxation (`ibex-fork/src/numeric/ibex_LinearizerXTaylor.cpp:115-118`,
  `sys.f_ctrs.jacobian(box)`), not affine arithmetic.
- The `pow(x,−3/2)` constraints **are included**, not skipped: `IbexConverter::ProcessPow` maps a
  non-integer exponent to IBEX `pow(ExprNode, double)` (`src/dreal/util/ibex_converter.cc:216-217`);
  `Convert` returns null only for ITE / uninterpreted / forall / forallT / integral
  (`ibex_converter.cc:323-415`) — none present here.
- **Why it's useless anyway:** the descent Jacobian entry `∂/∂dp` of the kernel carries
  `pow(‖Δp‖²,−5/2)` — a steep derivative whose interval over the box is enormous, so the X-Taylor
  linear bound is far too loose to contract. Empirically (INFO stats on the 6-free *solving* case):
  polytope = 3801 prunings, **2940 (77%) zero-effect**, 1.13 s; HC4 = 141k prunings, 0.023 s. Polytope
  is ~50× slower per call and mostly no-ops → net drag. `--acid`/`--s3b` (3BCID shaving) rest on the
  same HC4 interval eval → same looseness.
- **No affine arithmetic exists in this ibex-fork** (`grep Affine2|AffineMain src` → empty; only
  `LinearizerXTaylor/Compo/Duality/Fixed`). So the correlation-tracking relaxation that would tighten
  the `k·dp` product is **not available to switch to** — it would have to be ported/added.

**The LP *skips* the descent constraints (two independent IBEX/dReal source traces agree).** IBEX has
no native fractional-power op, so `pow(x,−3/2)` becomes `exp(−1.5·log(x))` (`ibex_Expr.h:1838`); its
symbolic gradient carries `1/x` (from `log`), which is **unbounded over any box hull where the kernel
argument touches 0**. `LinearizerXTaylor` then throws `BadConstraint` and omits the constraint from the
LP (`ibex-fork/src/numeric/ibex_LinearizerXTaylor.cpp:285-287`, caught 166). So the descent inequalities
contribute **zero** linear rows; only trivial bound/guard rows enter — matching the measured 77%
zero-effect polytope.

**Deeper root cause — diagonal geometry vs axis-aligned box.** The kernel argument is
`Δp = x1_px − x2_px`, a *difference* of two box vars each `[−4,4]`, so IBEX's box hull of `Δp` is always
`[−8,8] ∋ 0`. "Bodies apart" is a **diagonal** region, not an axis-aligned box — no L1 guard, explicit
`Δp²≥c`, or SAT box-exclusion on `Δp` can make the hull exclude the pole. Confirmed: **box-exclusion +
`--polytope` still walls**; the LP still can't bound the gradient because the hull still straddles 0.
Only pinning a whole body (`id_x2`) makes `Δp` a function of a *single* box variable (axis-aligned),
which is why that alone solved.

**Verdict on the algorithm question:** the current fork cannot crack this by flag choice — every exposed
contractor is gradient/interval-based, blows up on `pow(·,−3/2)`, and (for polytope) the constraint is
dropped from the LP entirely. Real fixes, cheapest first:
- **(c) generator sidestep** — instantiate ∀-params → sub-queries below the ~6-var cliff. No solver change.
- **(d) relative-coordinate reformulation — PROTOTYPED, partial win. Root cause: CSE is additive.**
  `cse_6 = x1_px − x2_px` (Δpx) *is* extracted and *is* a box variable — but CSE **adds** it without
  **removing** `x1_px, x2_px`. So the box carries **2 redundant absolute-position dimensions** (only the
  difference matters, translation invariance), and they are the coordinates that feed the gravity kernel —
  the hardest part of the search. ICP bisects them wastefully, and contracting `cse_6` does not
  back-propagate to collapse them (`x1_px = cse_6 + x2_px` is under-determined). Eliminating them —
  relative coords (`dpx, dpy ∈ [−8,8]`, equisatisfiable), or WLOG pinning one body's position — drops 10→8
  free and unlocks the identity case: **timeout → ~24–35 s**.

  **Clean isolation (all identity params):** 10-free (x2 pos free) → timeout for *both* L1 and box-exclusion
  guards; 8-free via pinning 2 **velocities** (`c8`) → *still* timeout; 8-free via pinning the 2 redundant
  **positions** → L1 24 s, box-excl 35 s; relative coords → 32 s. So the driver is **eliminating the
  redundant position dimensions specifically**, NOT the guard form (L1 is actually fastest at 8-free) and
  NOT "box-representability" (an earlier confounded claim — it compared 12-free-params-free against
  8-free-params-pinned). Caveats unchanged: works via **HC4, not the LP** (polytope 99.5% zero-effect even
  here, still a drag); partial (full params-free 12-var still times out — pairs with (c)). Net: emit the
  family in **relative coordinates** (drops the CSE-orphaned redundant abs-position dims; also the natural
  representation for translation-invariant dynamics) — a real, cheap generator-side win; combine with (c).
- **(b) custom monotonicity contractor** — `dp·(dp²+…)^{−3/2}` is monotone in `|dp|` beyond the pole.
- **(a) add an affine-arithmetic / interval-Taylor linearizer to the fork** — the general fix, biggest lift.

## Correction — my first two hypotheses were wrong (guard confound)

Initial "parameter-space singularity" and "same-term vs cross-term" stories were **artifacts of a
confound the user caught.** The negated claim's antecedent requires `sx·wx=1 ∧ sy·wy=1` (via `Int`
aux `cse_4,cse_5`) plus L1 `|Δpx|+|Δpy|≥3/2`. Pinning affine params *off* that manifold (e.g.
sx=0.7,wx=1.3 ⇒ sx·wx=0.91≠1) makes the antecedent false ⇒ implication vacuously true ⇒ **trivial
vacuous `unsat` in 0.06 s** — not a genuine reduction. The "solving" pins in the first sweeps were all
guard-violating. Re-run *on* the manifold (identity pin, sx·wx=1), they time out. Lesson logged per
`isolate-before-diagnosing`: the fast "solves" shared the confound.

## Remediation

**1. Flag remedy — none.** No NRA contractor flag rescues these. *Notable side-finding:* the harvest
ran them with `--forall-pre-prune` + forall-body-aware `--smear`, **both inert on QF files** (no
`Formula::Forall` node); the relevant levers are NRA-side, and those don't help either.

**2. Encoding fix — no equivalence-preserving re-encoding of the *same query* helps.** Flattening CSE,
inlining, and the on-shell identity (`wx·sx=1` ⇒ transformed kernel = original, saving ≤2 dims) all
stay above the ~6-var cliff (the query has 11–18). The only fix is to change the **query**, which is
the odeexpr_v2 generator's call (not edited here):
- **Instantiate, don't ∀-quantify, the affine params** — replace one 14-var ∀∀ query with several
  smaller fixed-param sub-queries (each ≤ the cliff). Sampling the affine group turns an intractable
  refutation into a batch of ~3 s ones.
- **Decompose** the 2-body refutation per body / per symmetry generator, or switch the verification
  approach for this family (SOS / Lyapunov certificate) — direct ICP whole-box UNSAT of an 11–18-var
  nonconvex NRA with a fractional-power kernel is at/beyond dReal's practical ceiling.

Reproduce: `…/exp/` holds all injected variants (`m_identity`, `only_d{1,2}`, `flat`, `id_x2`, `c7-c10`,
`pin_pos/pin_vel`, spring pins) and per-config `.log` files with `/usr/bin/time -p` footers.
