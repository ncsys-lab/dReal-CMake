# odeexpr_v2 `exists_forall` — Tractability Findings

Measured 2026-06-30 against `gcc_build/dreal4` (branch `ibex-transcendental-opt`).
Benchmark set: `~/Documents/new_dreal/ode_expressivity_energy/benchmarks/` (`odeexpr_v2`).

## TL;DR

The regenerated `odeexpr_v2` ∃∀ family is **intractable at its pinned precision
δ=0.0005, by design** — not a solver regression. The `forall/` directory is now empty;
`exists_forall/` holds 15 `∃c. ∀x. both_descend[…]` queries (provenance
`source: encoder-fix-intractable-subsetneq-ranged`, generated 2026-06-30, δ pinned via
`(set-option :precision 0.0005)` in each file). All 15 are non-terminating at δ=0.0005.
Enabling IBEX's SoPlex LP backend (`--forall-polytope`) helps at moderate δ but does not
bring the pinned precision into reach.

These queries therefore **cannot be baselined as timing data** — every run is a timeout,
and the two largest (`n3_h3` dh4/dh5, 1.6 MB / 8 MB) climb past 2.7 GB RSS and OOM at a
600 s budget. They were left out of the local baseline.

## What the queries are

Shape: `∃ J_*, ch*_* ∈ [-1,1]. ∀ x ∈ [-1,1]. (x² ≥ 1/400) ⟹ φ(c, x)`, where φ is a
conjunction of descent inequalities over `tanh`, `exp`, `pow`. Standard depth-one ∃∀ NRA
(`docs/forall-semantics.md`), handled by the CE-guided `ContractorForall`. Sizes scale
with `(n, d_h)`: from 1.5 KB (`n1`/dh1) to 8 MB (`n3`/dh5).

The smallest (`mlp2_n1_h1` dh1 — 3 existential vars, 1 universal) has a **trivial exact
witness**: all coefficients zero makes both descent expressions identically 0 ≤ 0. dReal
still cannot isolate it at tight δ.

## Measurements (smallest instance, `mlp2_n1_h1__…__dh1`)

Precision sweep, default brancher vs. `--forall-polytope`:

| δ | default brancher | `--forall-polytope` (SoPlex) |
|---|---|---|
| 0.5    | delta-sat, 0.19 s | delta-sat |
| 0.2    | timeout (>30 s)   | delta-sat, **21.7 s** |
| 0.15   | —                 | delta-sat, 39.1 s |
| 0.1    | —                 | delta-sat, 96.6 s |
| 0.0005 (pinned) | non-terminating | timeout (>60 s) |

- `--local-optimization` (CAV-2018 §3.3 locally-optimized counterexamples) gives **no**
  improvement — timeout at both δ=0.2 and δ=0.0005.
- Polytope runtime scales ≈ δ⁻²·² (each 1.5× tighter δ ≈ 2.5× slower). Extrapolating from
  δ=0.1 (96.6 s) to δ=0.0005 — 200× tighter — gives ~10⁵× ⇒ **months**, on the *smallest*
  instance. `n2`/`n3` (more existential vars) are strictly worse.

## Why — and the soundness/completeness framing

δ-complete ∃∀ (Kong, Solar-Lezama & Gao, CAV 2018; `docs/forall-semantics.md` §4) is
*guaranteed to terminate* but carries **no useful time bound**. The CE-guided loop must
bisect the existential box down to ~δ width to isolate a validating box, and each node runs
a nested ICP CE-search over `tanh`/`exp`. With δ=0.0005 over a 3-D box that is exponential.
The sharp cliff between δ=0.5 (0.19 s) and δ≤0.2 is exactly this bisection-depth wall.

Non-termination here yields **no verdict**, so it is neither a soundness nor a completeness
*violation* — just intractability. For completeness: every way the `forall` machinery can
go wrong *when it does answer* is COMPLETENESS-class (missed refutation → false `delta-sat`),
never SOUNDNESS — the pruning step contracts against a real forall-domain point, so it can
never delete a true ∃∀ solution (`docs/forall-semantics.md` §4.7).

## Build dependency

`--forall-polytope` requires IBEX's LP backend, which was `LP_LIB=none` (the contractor
compiled but threw `LPSolver method called but no LPSolver has been configured`). Switched
to the vendored SoPlex 4.0.2 in commit `fa3b74bd7` (`CMakeLists.txt`,
`-DLP_LIB=soplex`). Kept as a strict capability add for ∃∀ work generally, independent of
this family's intractability.

## Implication

To make this family tractable the **δ pin must be relaxed** (an encoder-side decision —
δ=0.0005 is tied to the math of the descent margin), or the family is accepted as a set of
permanent timeouts excluded from timing baselines. No solver-side lever in the current build
changes the verdict at δ=0.0005.

## `--forall-pre-prune` — the IBEX `CtcForAll` pre-pruner (2026-06-30)

Investigation of "can IBEX's native `CtcForAll`/`CtcExist` help here?" established **no for
this pinned family**, for a structural reason: those contractors quantify the *universal*
variable (dim 1→3), but the bottleneck is the *existential* witness search (dim 3→18), which
any approach must bisect to ~δ width. The measured `--forall-polytope` δ⁻²·² wall already
shows strong contraction can't escape it. `ibex::CtcForAll` was nonetheless wired as a sound
COMPLETENESS-only **pre-pruner** that runs beside CEGIS (`--forall-pre-prune`,
`ContractorIbexForall`; mechanism in `ibex_docs/AUDIT-QUANTIFIERS.md` Q1) — valuable for the
*broader moderate-δ ∃∀ workload*, not this family.

### Encoding-dependence: the speedup was real but NOT robust across re-encodings

On the **first** `odeexpr_v2` encoding, pre-prune was dramatic on `mlp2_n1_h1` — δ=0.2 went
114 s → 1 s (~100×), δ=0.35 39 s → 1 s — with no verdict flips. **The 2026-06-30 11:21
re-encoding (`encoder-fix-intractable-subsetneq-ranged`, changed descent inequalities)
erased that headroom**: rewritten `n1_dh1` is now **0 s at every δ from 0.5 down to 0.1**
for *baseline* — so there is nothing left to speed up. Re-validated across the 15 rewritten
files:

- **Every instance is SAT** (the all-zero MLP — `J=ch=0` ⇒ both descent products `0 ≤ 0`
  for all x — is an exact witness; pinning all existential vars to 0 returns `delta-sat`
  at the pinned δ=0.0005 even on the 12 MB `n3_dh5`). The family is **SAT-by-construction**,
  not a refutation target.
- **At the pinned δ=0.0005, all 15 time out** — pre-prune, polytope, local-opt, and their
  combinations included. Unchanged from the first encoding: the existential δ-isolation wall.
- **On the cases that still have runtime headroom** (`n1_dh4/dh5`, `n2_dh1`: timeout at
  δ=0.5/120 s), pre-prune **rescues none** — they time out with or without it.
- **`--forall-polytope` now *hurts*** on this re-encoding: `n1_dh3` at δ=0.5 is 19 s
  baseline but **times out** with polytope (alone or with pre-prune). local-opt is neutral.

**Takeaway:** the pre-pruner is sound and can help *some* moderate-δ ∃∀ encodings a lot, but
the benefit is **encoding-specific and fragile** — re-validate per workload, never assume it
carries. It does **not** address this family's actual hardness (existential isolation at
tight δ), and it cannot turn these SAT instances into the unsats a separation proof needs —
that is an encoder-side question (exclude the trivial zero-MLP witness / target the ¬≼
direction), not a solver lever.

**Flat NRA corpus (2026-06-30 differential net, `benchmark/forall_differential.sh`):** across
the 24 `exist_forall_*` / `ea_*` / `github_issue_18*` / `cgd8d_*` / `minimize_02` instances at
δ=0.001, under {jobs 1,2,4} × {±pre-prune} × {±polytope}, **zero verdict flips** — the soundness
property holds corpus-wide and across the forall+parallel combination. The same "can hurt"
fragility shows here too: `--forall-pre-prune` slows `exist_forall_10` from <90 s to **>300 s**
(does not finish) and `exist_forall_zenna_01` into the 90–300 s range, both **preserving the
`delta-sat` verdict**. Off-by-default; a runtime concern, never a soundness one.

**H1 CE-domain fix — the perf cliff and the narrow-conditional resolution (2026-06-30 A/B).**
The forall CE search formerly ε-shrank the *universal domain* (`domain^{-ε}`), a silent
completeness bug on narrow/point domains (`forall-semantics.md` §4.8). Making the domain **exact**
(full CAV-2018) is correct but blows up the search on wide-domain ∃∀: on `mlp2_n1_h1 dh3` at
δ=0.5, base→exact went **18 s → 171 s (9.4×)** and a sibling `dh3` **6.6 s → >300 s (timeout)`**,
**verdicts unchanged**. At δ≤0.1 the family is intractable for *both* (the existential wall), so
this cost is invisible at the pinned δ=0.0005 but real in the δ=0.5 tractability-probing regime.
Resolution (owner's call): keep the domain exact **only for narrow variables** (binder width
`< 3ε`, where the shrink empties/degenerates the domain — the actual hazard); wide variables keep
the fast ε-shrink. This restores δ=0.5 perf (`dh3` back to **18.6 s ≈ baseline**) while fixing the
narrow/point hazard. COMPLETENESS-only throughout; soundness is never at stake.

- **No verdict flips** observed across all runs — sound, as argued (proj-inter contracts
  only against real universal points; the implication guard is preserved by the recursive
  `∨`→`CtcUnion` build). Unit test + mutation:
  `test/dreal/contractor/test/contractor_ibex_forall_test.cc`.
- **`--forall-pre-prune-prec` is near-irrelevant on this family** (audit Q4): `n1_dh3` at
  δ=0.5 is ~18–19 s flat for prec ∈ {0.25, 0.5, 1.0, 2.0, 4.0} — the universal check isn't
  the bottleneck. Mechanical ceiling: prec ≥ the universal-box width (2.0 for x∈[-1,1])
  makes `LargestFirst` non-bisectable, collapsing `CtcForAll` to a single midpoint check at
  x=0; prec=4.0 ≡ prec=2.0. So **no reason to raise it above the box width** — and for an
  *unsat* goal the heuristic *inverts* (finer prec samples more universal points ⇒ stronger
  refutation), so "bigger = faster" is a SAT-only artifact. Default stays **0.5** (the only
  measured harm from too-fine was the first encoding's prec=0.1 δ=0.2 regression).
- **Off by default**; NRA ∃∀ only; runs under `--jobs>1` via a per-worker
  `ContractorIbexForallMt` cell (one `ibex::CtcForAll` per thread).
