# dReal4 Optimization Log (`upgrade-ibex-opts`)

Iterative profile → optimize → benchmark loop. Each idea lands in **Adopted** (committed)
or **Rejected** (with numbers + reason). Acceptance bar: net PAR2 improvement ≈≥10% on the
probe + gate sets, **zero** SAT/UNSAT correctness flips vs ground truth, no >1.5× regressions.
Soundness guardrails (FE_UPWARD/FE_TONEAREST guards, no silent fallbacks) are non-negotiable.

> **Scope:** §Adopted and §Rejected below are **all CAPD/ODE-path tuning** — the probe set
> is ODE-heavy and the baseline profile is an ODE benchmark where `contractor_ode_lohner`
> is 90.8% of runtime. The ODE-free `ode_expressivity` (odeexpr) family shares **none** of
> this code; for its assumption re-check and its (different) hotspot, see the
> **odeexpr** section at the bottom of this file.

## Measurement setup

- **Baseline:** `benchmark/baseline_local.csv` (~30 stratified, refreshed on this branch).
  The previously-committed local baseline (at `213394110`) was **stale** — it recorded the
  pre-FE_UPWARD-fix false-UNSAT/crash results for `k20/k28/k40 box`; the refreshed one shows
  the correct delta-SAT.
- **Probe set:** `benchmark/probe_odes.tsv` (18 ODE-heavy benchmarks; tacas inverters are the
  most CAPD-sensitive). Own frozen baseline: `benchmark/probe_baseline.csv`. Compare with
  `benchmark/run_probe.sh` → `benchmark/probe_compare.py`.
- **Profiling:** the Release binary (`gcc_build/dreal4`) keeps full function symbols (not
  stripped), so macOS `sample <pid> <secs>` gives function-level attribution with **no**
  separate RelWithDebInfo build needed. (A `prof_build/` RelWithDebInfo dir is only worth
  adding if line-level detail becomes necessary.)
- **Sentinels:** `1mhz_k20/k28/k40 …box…` (the formerly-false-UNSAT trio) are kept in the probe
  set as **soundness sentinels** — if any change re-introduces false-UNSAT they flip to UNSAT.

## Baseline profile (the map we're optimizing against)

`sample` of `tacas_c2e2_0hz_k22_…NOR__sigmoid…SAT` (~112 s baseline, IcpSeq path), 30 s window:

| Frame | % of samples | Note |
|---|---|---|
| `IcpSeq::CheckSat` | 100% | sequential ICP (this benchmark) |
| `contractor_ode_lohner::Prune` | **90.8%** | the ODE contractor is the bottleneck |
| `run_capd_fwd` | 85.4% | forward CAPD integration (BWD contractor is the small remainder) |
| `OdeSolver::encloseC0Map` | 84.5% | |
| `computeTaylorCoefficients` (order 20) | **74.2%** | **primary target** — scales with Taylor order |
| `autodiff::Div::eval` | ~17% | vector-field division AD (inverter sigmoid has `/`) |
| `DoubleRounding::roundUp/roundDown` (leaves) | ~9.3% | per-op FPU-mode switches (CAPD NATIVE intervals) |
| `capd::intervals::operator*` (leaves) | ~6.9% | interval multiplies |

Takeaway: CAPD forward Taylor integration dominates nonlinear-ODE runtime. Highest-value levers
(profiling-justified): **lower Taylor order**, **looser tolerance** (fewer steps), and reducing
the AD operation count. Set type is already `C0Rect2Set` (doubleton + QR reorganization).

## Backlog (ordered; profiling-justified first)

1. Centralize the 3 duplicated CAPD config sites into one helper (refactor; behavior-identical). — prerequisite
2. Lower Taylor order 20 → {16,14,12,10} (sweep). Targets the 74% hotspot directly.
3. Looser tolerance 1e-10 → {1e-9,1e-8,1e-7}. Fewer steps.
4. Joint order×tolerance sweep.
5. Adaptive `n_steps` clamp `[20,60]` / factor `2.0` retune.
6. Set representations: `C0Rect2RSet` / `C0HORect2Set` / tripleton (tightness vs cost).
7. `SolutionCurve` reuse; warm-start step size across ICP iterations.
8. C1/variational backward narrowing (interesting, higher effort).
9. Allocation reduction in `with_params` IMap deep-copy per Prune.

---

## Open approaches (not yet attempted)

### Vector-field simplification / CSE before to_capd_string

The forward integration is ~69% of order-10 runtime, dominated by
`computeODECoefficients` — CAPD's automatic differentiation of the ODE RHS, with
`autodiff::Div` (division AD) alone ~17%. CAPD's parser does common-subexpression
elimination *within* one IMap string but does not factorize. The ODE RHS for the
inverter/cardiac models has large repeated transcendental subterms (the same
`log(... exp ...)` block appears across multiple `d/dt`). Pre-simplifying / CSE-ing
the RHS with Drake's symbolic layer before emitting `to_capd_string` could cut the
per-step AD cost at its root. Medium-high effort, **low confidence**:
- The `a / c → a * (1/c)` constant-denominator rewrite is **N/A** for the probe
  families — their divisions are state-dependent (prostate `(/ z (+ z 2))`, the
  inverter's 60 divisions are sigmoid terms), so the expensive division AD is
  inherent, not a constant-fold artifact.
- Subexpression dedup: CAPD's parser already does within-string CSE, so a
  Drake-level CSE pass may add little. Would need to confirm CAPD isn't already
  capturing the repeated `log(...exp...)` blocks before investing.

This is the last untried CAPD-side idea and it is speculative; the high-confidence
config/allocation wins are all harvested.

## Adopted

### 1. Taylor order 20 → 10  (commit pending)

Lower the CAPD `IOdeSolver` Taylor order from 20 to 10 (`kCapdTaylorOrder`).
Directly attacks the 74% `computeTaylorCoefficients` hotspot — fewer Taylor
coefficients per step on the expensive transcendental/division vector fields.

**Sweep (fast sub-probe, 16 benchmarks, vs order-20 frozen probe_baseline):**

| order | net PAR2 | flips | notes |
|---|---|---|---|
| 14 | 0.671 (32.9% faster) | 0 | tacas inverters ~1.85× |
| **10** | **0.517 (48.3% faster)** | **0** | tacas inverters ~3× (0.31–0.33×) |
| 8 | 0.446 (55.4%) | **1** | rejected — prostate SAT→UNSAT |

**Validation at order 10:**
- Full probe (18, incl. 2 TIMs): net 0.859 (14.1% faster), 0 flips, 0 real
  regressions. The 2 TIMs (`quad2-1`, `k13_inverter`) stay TIM — they're ICP/
  SAT-search bound, not CAPD-per-step bound, so order doesn't rescue them.
- 30-set gate: **0 correctness flips**, 7 exceptional. One >1.5× "regression"
  (`car-3-single-linear` 267s→TIM) was **parallel-scheduling contention, not an
  order effect**: isolated, car-3 solves delta-sat in **184s** at order 10
  (well under the 300s timeout). car-3 is a near-timeout linear benchmark whose
  parallel PAR2 is noise-dominated.
- ctest green except the documented flaky trio.

Soundness: a lower-order Taylor enclosure is wider but still a rigorous
superset — sound, never a false-UNSAT. The order-8 prostate flip is the
delta-sat/unsat boundary ambiguity (different orders give different enclosure
*shapes*); order 10 keeps prostate SAT consistently across orders 10/14/20.

Current best = order 10. Subsequent experiments measure vs the order-20 frozen
probe_baseline, so their net ratio reflects cumulative gain; compare against
0.859 (full) / 0.517 (fast) to detect incremental regressions.

### 2. thread_local reuse of the parameter-bound IMap (commit pending)

`with_params` deep-copied the cached IMap (the full automatic-differentiation
tree) on every fwd/bwd/trace call to bind parameters without mutating the
shared cache — ~part of the ~8% allocation churn at order 10 (relatively larger
once the integration itself got cheap). Replaced the per-call deep copy with one
reusable copy per (thread, base map) in a `thread_local` cache, re-binding only
the parameters (cheap `setParameter`) each call.

Strictly safe / behavior-identical: `setParameter` fully overwrites the named
parameters, the cached base maps are immutable and live for the whole process,
and thread_local storage means no copy is shared across parallel ICP workers
(works for IcpSeq and IcpParallel). No verdict can change — it only removes an
allocation.

- Fast sub-probe: net 0.499 vs order-10's 0.517 (~3.5% faster), 0 flips.
- Full probe: net 0.847 vs order-10's 0.859 (15.3% cumulative vs order-20), 0
  flips, 0 regressions, TIMs unchanged.
- ctest green except the flaky trio.

## Rejected

### C1 / variational integration (C1Rect2Set forward)

Scoped per the "interesting new contractor algorithm" direction: use the forward
integration's variational data (the monodromy ∂x_t/∂x_0) to narrow X_0 instead of
a separate backward integration. Tested the optimistic lower bound first — switch
the forward set to `capd::C1Rect2Set` (computes the monodromy) and *discard* the
monodromy, measuring only the integration-cost penalty / enclosure-tightness
effect:

- k17 alone: 27s→20.7s (faster — misleading single sample).
- Fast sub-probe: net **1.658 (65.8% SLOWER)**. The tacas inverters speed up
  (0.28–0.29) but **prostate goes SAT→TIM (13.4s→300s)** — the variational
  integration explodes/diverges there. A severe regression that dominates.

Rejected. The probe benchmarks have n≈6 integrated state vars, so C1 integrates
a 6×6 monodromy (~an order of magnitude more interval arithmetic than C0) and is
prone to variational blow-up on stiff dynamics. This was already the *optimistic*
case (free tighter enclosure, no interval-Newton backward yet); the full
variational-backward would add cost on top of an already net-losing forward.
C1/variational is not viable for these dynamics. (No IC1OdeSolver typedef in
CAPD; C1 comes via integrating a C1Rect2Set with the C0 IOdeSolver.)

### Set representation: C0HORect2Set (Hermite-Obreshkov) at order 10

Fast-probe net 0.532 vs order-10's 0.517 — marginally *worse*, 0 flips. The
tighter HO enclosure did not reduce ICP work on the tacas inverters (identical
0.31–0.33 — they already prune fine at order 10) and added per-step corrector
cost elsewhere (prostate 8.2s→11.3s). Confirms enclosure *tightness* is not the
bottleneck at order 10; per-step Taylor cost is. C0TripletonSet (more per-step
cost, same mechanism) not tested — same prediction. Kept C0Rect2Set. The
`CapdC0Set` type alias was added to centralize this knob for the A/B test and
is retained (mirrors the order/tolerance centralization).

### Re-profile at order 10 (guides remaining work)

After order 10, the k22 heavy path shifted: contractor_ode_lohner::Prune 79%
(was 91%), run_capd_fwd 69% (was 85%), backward contractor + Prune overhead
~10% (was ~5%), non-ODE (ibex arithmetic HC4 + fixpoint) ~21% (was ~9%). The
forward Taylor cost is at the order-10 floor and irreducible by config. The
two grown shares — the **backward contractor** (a second full CAPD integration
per ODE constraint) and **non-ODE arithmetic** — are the remaining targets.

### Backward ODE contractor — no safe focused win (cav26 X_0 narrowing kept)

The backward contractor (a one-shot `-f(x)` image narrowing X_0, one per ODE
constraint, interleaved with a full `nl_ctcs` sweep in the plain fixpoint) is
~10% of solve time directly and ~16% including its trailing nl sweeps. Three
ways to cut it, all rejected:

- **Disable it entirely:** fast-probe 0.432 (56.8% faster), full probe 0.826,
  30-gate 0 flips / 0 regressions / 15 exceptional — clean on all 48 benchmarks.
  But this *removes* cav26's X_0-narrowing capability; 48 benchmarks from 3
  families can't represent the space, and the risk is completeness flips
  (UNSAT→SAT) on unseen instances (it can never cause false-UNSAT — removing
  pruning is always sound). User chose to preserve the capability. Not adopted.
- **Cheaper via lower backward order (kCapdBackwardOrder=6):** dead end. Net
  0.511 ≈ no speedup (the one-shot backward is dominated by fixed overhead —
  IMap copy, set construction — not order-sensitive Taylor work like the
  forward multi-step pass), AND it flips prostate SAT→UNSAT (a lower-order
  enclosure is tighter in some projection). Rejected.
- **Conditional via worklist fixpoint (`--worklist-fixpoint`):** catastrophic
  variance. k17 (SAT inverter) 23.4s→0.85s (27× faster) but k70 (UNSAT saradc)
  47s→**7876s** (167× slower, 2.2h). Off-by-default for good reason; unsafe as
  a global change. Rejected. (A custom stateful per-constraint gate is too risky
  given this variance.)

Net: backward narrowing stays on at order 10. The remaining CAPD targets are
allocation (`with_params` IMap deep-copy, ~part of ~8% malloc at order 10) and
big-effort algorithmic work (C1/variational backward narrowing reusing the
forward solution curve).

### Taylor order 8 (and below)

55.4% faster on the fast sub-probe but flips `github …prostate_h2` SAT→UNSAT
vs the order-20 baseline — a correctness flip (halt). The flip is the inherent
delta-boundary ambiguity rather than a soundness bug (the enclosure stays a
valid superset at any order), but any verdict change vs baseline is
disqualifying. The order knee is between 8 and 10; order 10 is the floor with
zero flips. Not retested below 8.

### CAPD tolerance 1e-10 → 1e-8 (at order 10)

No effect: fast-probe net 0.513 vs order-10's 0.517 (0.4% = noise), 0 flips.
**Why (important):** the integration step size is NOT tolerance-limited for
these benchmarks. `run_capd_fwd`/`run_capd_bwd` compute `n_steps`/`max_step`
(`adaptive_n_steps`) but **never pass them to the solver** — they are dead code
(only the `if (max_step <= 0)` guard uses them; `n_steps` is live only in
`run_capd_trace` for `--visualize` slicing). CAPD integrates with its own
tolerance-based adaptive control, and for these short/smooth horizons the step
count is already near-minimal, so loosening tolerance can't reduce it further.
Both the tolerance and n_steps levers are therefore closed for fwd/bwd; the
per-step Taylor-coefficient cost (order) is the only step-cost lever, and it's
at the order-10 floor. Kept tol 1e-10 (tighter = safer, no speed cost).
Follow-up: the dead `n_steps`/`max_step` in fwd/bwd is a cleanup candidate
(also the stale Codac-era file header comment).

---

## odeexpr (ODE-free QF_NRA) — assumption re-check (2026-06-20, HEAD `8a2367182`)

**Why this section is separate.** Everything above is CAPD/ODE-path tuning. The
high-priority `ode_expressivity` (odeexpr) family is **pure QF_NRA with no ODEs** (50
`.smt2`, transcendental-heavy: `sin`/`tanh`/`pow`/`exp`; no quantifiers; each sets
`:precision 5e-4`; Lyapunov positivity/stability/decrease obligations). It exercises a
different code path, so the question "could any §Adopted/§Rejected idea backfire here?"
needed a direct check.

**Orthogonality — confirmed by measurement, not just by reading.** `sample` (20 s) of 5
hard odeexpr benchmarks shows **0 samples** in any `capd*` / `run_capd*` /
`contractor_ode*` frame. The active path is
`IcpSeq → Fixpoint[ ContractorIbexFwdbwd × N, Integer ] → BranchLargestFirst`,
single-threaded, with polytope / local-opt / pattern-matching all off by default
(`run_batch.sh` passes no flags; `drpm_max_size` default 0). Every §Adopted/§Rejected idea
(Taylor order, thread_local IMap reuse, C1 variational, Hermite-Obreshkov, CAPD tolerance,
backward contractor, worklist-fixpoint, vector-field CSE) is in CAPD code that **never runs
here** — none can help or backfire. *Correction to the §Adopted note: the thread_local
IMap-reuse win is **neutral** on odeexpr, not beneficial — `IMap` is CAPD's map, built only
for ODE constraints.*

**The real odeexpr hotspot — two mechanical overheads the ODE campaign never saw.**
Self-sample leaf attribution:

| benchmark (verdict) | `fesetround` | exc-unwind | gaol arith | IBEX HC4 | malloc |
|---|---|---|---|---|---|
| size_sweep.kuramoto_doe_N5 (sin) | **44%** | 3% | 13% | 17% | 3% |
| box_sweep.tanh_decrease_xwin2 | 37% | 24% | 6% | 13% | 3% |
| box_sweep.tanh_decrease_J1 (SAT) | 37% | 22% | 7% | 14% | 2% |
| tanh.decrease_slope | 23% | 31% | 9% | 14% | 3% |
| tanh.composite_lipschitz_i1 | 30% | **37%** | 5% | 9% | 4% |

1. **`fesetround` (FPU rounding-mode switch), 23–44%.** Verified caller chain
   `gaol::cos → fesetround` (leaves `dubsin`/`ucos`/`uacos`): gaol's **ARM64 interval
   transcendental functions switch the rounding mode per call** for directed-rounded bounds.
   odeexpr is transcendental-dense and each ARM64 `fpcr` write is pipeline-serializing. This
   is *gaol-internal* (vendored interval lib), one level below dreal's phase-hoisted
   `UpwardRoundingScope`, so the existing dreal-side rounding optimization does not reach it.
2. **C++ exception unwinding, 3–37%.** Verified `__cxa_throw` / `_Unwind_RaiseException` /
   `__gxx_personality_v0` on the hot stack (+ the dyld per-frame image-lookup cluster the
   unwinder uses). Origin: IBEX `HC4Revise.cpp` `throw EmptyBoxException()` (10+ sites in the
   backward path), fired on every prune-to-empty — which dominates the UNSAT decrease proofs.
   The dreal QF_NRA path doesn't catch it (`rounded_interval.h` `ibex_hc4_backward` forwards; `fwdbwd.cc:139`
   reads `is_empty()`); the throw is caught/converted inside IBEX's callback-backward, per
   prune.

Combined, **~half of odeexpr runtime is mechanical overhead** (mode switches + unwinding);
the actual interval algebra (gaol arith + HC4) is only ~20–30%. This is the inverse of the
ODE families, where CAPD Taylor integration (90%+) buried both. **Future odeexpr work should
target these two** — a gaol transcendental path that avoids per-call `fesetround` on ARM64,
and an empty-domain *signaling* path that returns a flag instead of throwing — not anything
in §Adopted/§Rejected.

**A/B of the shared / default-off levers** (HEAD `8a2367182`, fresh clean-src build, all 50,
600 s wall timeout, IcpSeq; reference = default flags):

| arm | solved | SAT | UNSAT | TIM | PAR2 vs default | verdict flips |
|---|---|---|---|---|---|---|
| default | 36/50 | 11 | 25 | 14 | 1.00× | — |
| `--worklist-fixpoint` | 34/50 | 10 | 24 | 16 | **5.58× worse** | none |
| `--polytope` | 16/50\* | 0 | 16 | — | n/a (errors) | none |

- **`--worklist-fixpoint`: net negative — the log's ODE-grounds rejection holds on odeexpr
  too.** ~2× faster on the 12 commonly-solved (aggregate 0.49×) but pushes
  `tanh_decrease__J1.0` (SAT 311 s → TIM) and `kuramoto_doe__N3` over the timeout, losing 2
  solves — the same faster-on-some / catastrophic-on-others variance as k17/k70. No flips.
- **`--polytope`: not a usable lever in this build.** \*The 16 "solved" are trivial instances
  solved before the contractor fires; the rest exit 255 with `error: LPSolver method called
  but no LPSolver has been configured` — IBEX was built without an LP backend (`-DLP_LIB`
  unset). Evaluating polytope here (LP cuts might help the transcendental constraints) would
  first require rebuilding IBEX with an LP solver.
- **`--local-optimization`: not run — provably inert** (exist-forall-only per `--help`;
  odeexpr is quantifier-free).

**Bottom line.** No §Adopted/§Rejected decision can have the opposite effect on odeexpr —
they are dead code for it. The one rejected lever that *shares* the QF_NRA fixpoint
(`--worklist-fixpoint`) was re-tested directly and is net-negative here too, with no
soundness flip. The genuine headroom is elsewhere (gaol ARM64 transcendental `fesetround`,
EmptyBoxException unwinding), untouched by this log.

## odeexpr `fesetround` — a dReal-side slice was reachable after all (2026-06-20, HEAD `6d218633b`)

**Refines the "`fesetround` 23–44%, all gaol-internal" claim above.** The earlier
attribution chased `gaol::cos → fesetround` on the **`sin`-heavy** benchmarks (kuramoto),
which is genuinely gaol-internal and off-limits. But on the **`pow`-heavy** benchmarks
(sigmoid, the `cs*` family) a *separate* slice of the `fesetround` cost was **dReal-side and
removable**. Hard data (ARM64, this machine):

- **Micro-benchmark** (`fesetround`/`fegetround` in a tight loop): `fegetround` ~1 ns;
  `fesetround` same-value ~5 ns; `fesetround` changing-value ~11 ns. The read is ~5–11× cheaper
  than the write, and a "check-before-set" branch in the redundant case runs at *read* speed.
- **Guard-construction census** (instrumented `RoundingModeGuard`): the `FE_UPWARD` (interval)
  regime is already fully phase-hoisted (0–6 guard constructions per *entire solve*). **All**
  guard volume is `FE_TONEAREST`, and return-address attribution pinned **100% of the
  genuine** (mode-actually-changed) nearest flips to a single caller: **`is_integer`**.
- **Source**: `ExpressionEvaluator::VisitPow` calls `is_integer(exponent)` per `pow` to pick
  integer- vs real-power. `is_integer` (and `convert_int64_to_double`) opened a
  `NearestRoundingScope` *for uniformity* — but they are mode-**independent** (`modf` is an
  exact split; comparisons and `== 0.0` are exact; int→double within ±2^53 is exact). Under the
  `FE_UPWARD` eval phase, each call was a needless `FE_UPWARD→FE_TONEAREST→FE_UPWARD` flip.

**Fix (adopted).** Two changes, both sound (Debug rounding gate clean, full suite green,
no verdict changes vs HEAD on int/continuous/forall spot-checks):

1. **Removed the spurious `NearestRoundingScope` from `is_integer` / `convert_int64_to_double`**
   (`util/math.cc`) — the real win, since `is_integer` was the sole hot genuine-flip source.
2. **Check-before-set in `rounding_detail::RoundingModeGuard`** (`util/rounding.h`) — the
   ctor's existing `fegetround` save gates the entry `fesetround` on whether the requested mode
   differs, and the dtor restore is live-based: it reads the live FPCR (which it needs anyway for
   the always-on clobber tripwire later folded into the same dtor — see the FPU-rounding section
   of `CLAUDE.md`) and skips the write when the mode is already correct. Either way a *redundant*
   nested scope (already-correct mode) costs zero writes. General hygiene; makes the remaining
   redundant nearest scopes free. (The original `changed_`-gated restore was superseded by the
   live-based form when the tripwire + `ExpectClobber` containment landed.)

**Measured effect.** `sample` of the `pow`-heavy `cs5c_sigmoid__decrease` (4 runs each):
`fesetround` share **25–27% → 23–24%** (~2 pp absolute, ~9% relative), i.e. a few-% CPU
recovery on `pow`-dense instances. The remaining ~24% is gaol's own `pow`/transcendental
directed rounding — *that* part really is gaol-internal and untouched.

## odeexpr `fesetround` — the gaol-internal slice, addressed in the ibex-fork (2026-06-20)

The "gaol-internal and untouched" remainder above was made tractable by patching the vendored
gaol itself (two surgical, **bit-identical** levers in `ncsys-lab/ibex-lib@dreal-perf-patches`;
catalogued as patches #9/#10 in `../ibex-fork/MIGRATION.md`). Each interval transcendental
(`gaol::cos`/`exp`/`tan`/`sinh`/…) toggles the FPU mode nearest⟷upward around every
correctly-rounded mathlib call; on ARM64 each toggle was a libc `fesetround` whose `msr fpcr`
is pipeline-serializing.

- **Lever 1 (`ibex-fork@e0311233`):** inline aarch64 `mrs`/`msr` FPCR-RMode write replacing the
  libc `fesetround` in gaol's `round_{nearest,upward,downward}` (same instruction sequence as
  macOS `fesetround`, minus the call frame).
- **Lever 2 (`ibex-fork@3902fa35`):** batch the two directed bounds of each transcendental into a
  single `round_nearest()`/`round_upward()` pair (`<f>_dn_up` helpers), halving the toggle count
  (~4→2 per transcendental).

**Soundness:** bit-identical by construction (levers change only *when/how* the mode switches,
never a computed value), enforced by a dReal-side gate `test/dreal/util/test/
gaol_transcendental_bitidentity_test.cc` (+ committed golden, dReal `f55d48057`): a 132-case
adversarial grid whose output endpoint bits must reproduce the pre-lever baseline bit-for-bit.
Verified bit-identical on both levers; full ctest clean; clobber tripwire satisfied.

**Measured (3-way, 36 baseline-solvable odeexpr, CPU time, same machine):** L1 alone ≈ **2%**
aggregate (the `msr` *serialization*, not the call frame, dominates — so inlining the call buys
little); L1+L2 ≈ **8%** aggregate and **7.5–11.8%** on the transcendental-dense long-runners
(`tanh_decrease__J1.0` 264→245 s, `kuramoto__N5` 113→101 s, `kuramoto__N4`/`kuramoto_doe__N3`
~−10–12%). Lever 2's toggle-halving carries the win. Both levers retained.

## odeexpr `EmptyBoxException` unwinding — eliminated in the ibex-fork (2026-06-20)

**Closes the second headroom flagged above ("EmptyBoxException unwinding, untouched by this
log").** IBEX's forward-backward contractor (`HC4Revise`) signalled "a domain emptied" by
**throwing** a (protected, nested) `EmptyBoxException`; UNSAT-style decrease/positivity proofs
prune to empty at extreme frequency, so the per-throw C++ unwinding machinery
(`__cxa_throw`/`_Unwind_*`, table-based on ARM64) was paid on the ICP hot path. macOS `sample`
on the throw-heavy long-runners measured `__cxa_throw` *inclusive* at **26.6%** of CPU on
`tanh_decrease__J1.0` and **5.2%** on `kuramoto__N5`.

Replaced the exception control flow with a **return-status** signal (no `thread_local`, no
globals — upstream-clean), in two stages:

- **Tier-0** — convert only the shallow **root-intersection** throw (`HC4Revise::backward`,
  which also captures forward-undefined empties funnelled through `Eval`) to `return false`;
  `proj` detects it via `d.top->is_empty()`. Measured: `tanh_J1` 26.6% → **11.2%**, `kuramoto`
  5.2% → **0.1%**. Profiling then showed the **deep `*_bwd` throws** (`mul_bwd`/`sub_bwd`) still
  cost ~11% on `tanh_J1`, so:
- **Phase-2** — convert the whole shared backward engine to a `bool` return contract:
  `CompiledFunction::backward<V>` short-circuits on the first `false`; every `*_bwd` in
  `HC4Revise`, `InHC4Revise`, and `Gradient` (the three `BwdAlgorithm` visitors the driver is
  instantiated for) returns its primitive's bool; the nested `EmptyBoxException` classes and all
  `try/catch` are removed. Public `Function::backward(y,x,cb)` keeps its signature, so dReal is
  unchanged (it already detected emptiness via `iv.is_empty()`). Measured: `__cxa_throw`
  inclusive **→ 0%** on both `tanh_J1` and `kuramoto`.

**A/B payoff (Phase-2 vs pre-change baseline, all 50 odeexpr, CPU time, `timeout 600`):**
**zero SAT/UNSAT flips, zero regressions.** Solve set **14 → 12 TIM**: `tanh_decrease__J0.6`
(TIM → SAT 451 s) and `cs5c_sigmoid__decrease` (TIM → UNSAT 580 s) now solve; nothing newly
times out. Speedups: `tanh_decrease__J1.0` 311 → **183 s (1.7×)**, `cs4_equivalence__decrease`
1.26 → 0.71 s. The two newly-solved are the throw-densest decrease proofs — exactly where the
unwinding cost was concentrated.

Lives entirely in the ibex-fork (`src/function/ibex_{HC4Revise,InHC4Revise,Gradient,
CompiledFunction,BwdAlgorithm,Function}.{h,cpp}`); catalogued as the Tier-0 and engine-
conversion patches in `../ibex-fork/MIGRATION.md`. Guarded by the Phase-0 soundness net
(`test/dreal/contractor/test/contractor_*_test.cc`, `…/ibex_backward_callback_partial_empty_test.cc`,
`test/dreal/api/test/hc4_empty_propagation_soundness_test.cc`, and engine-level
`empty01/empty02` in ibex `tests/Test{HC4,InHC4}Revise.cpp`), all of which were written against
the throw-based code first and stayed green through both stages with zero assertion edits.

---

## odeexpr post-Phase-2 profile (2026-06-20)

**What changed.** With `fesetround` and `__cxa_throw` addressed, the two known hotspots
both read ~0% in a fresh post-Phase-2 `sample`. Three benchmarks profiled
(macOS `sample`, 30 s windows, leaf-level attribution):

| category | xwin1.5 (TIM) | kuramoto__N6 (TIM) | J0.6 (SAT, 451 s) |
|---|---|---|---|
| gaol transcendentals (atanh/tanh/cos/sin/div_rel/sqrt_rel/uipow) | **44.8%** | **30.5%** | **45.2%** |
| HC4 backward (add/sub/mul/tanh/proj bwd + CompiledFunction::backward) | 11.4% | 12.0% | 12.0% |
| HC4 forward (Eval::mul/add/sub/tanh_fwd + CompiledFunction::forward) | 9.3% | 13.6% | 8.9% |
| ExpressionEvaluator (Drake VisitExpression / VisitPow / accumulate) | 6.9% | 4.5% | 6.6% |
| Allocation (_xzm_free / IntervalVector copies) | 5.4% | 5.2% | 5.2% |
| gaol interval arithmetic (operator\*=/+=-=) | 5.1% | 6.2% | 4.7% |
| **Timer guards** (`ContractorIbexFwdbwd` stat.timer_pruning) | **4.1%** | **16.3%** | **4.0%** |
| libsystem_m (tanh/log1p/atanh/nextafter — called by gaol) | 4.1% | 1.8% | 3.9% |
| fesetround / FPCR | **~0%** | **~0%** | **~0%** |
| `__cxa_throw` / unwind | **~0%** | **~0%** | **~0%** |
| Branching (FindMaxDiam) | 0.5% | 0.1% | 0.4% |

**Key findings:**

1. **Both prior hotspots confirmed gone.** `fesetround` and `__cxa_throw` show 0% across all
   three benchmarks. ✓

2. **Gaol transcendentals now dominate (30–45%).** `gaol::atanh` (19%), `gaol::tanh` (13%),
   `gaol::cos`/`acos_rel` (10% each on kuramoto), `gaol::div_rel` (3–5%), `gaol::sqrt_rel`
   (2%) — this is the actual interval computation. The libsystem_m share (~4%) is the
   underlying correctly-rounded math calls within gaol. Together they are the **computational
   floor**: cannot be reduced without changing the interval library's soundness semantics.

3. **New: Timer guard overhead (4–16%).** `ContractorIbexFwdbwd::Prune` called
   `stat.timer_pruning_.resume()` and `.pause()` unconditionally (lines 100, 133), bypassing
   the `stat.enabled()` gate — 2× `std::chrono::steady_clock::now()` → `mach_continuous_time`
   per `Prune` call. With default spdlog level `off`, `stat.enabled() = false`, so the timer
   information was computed and discarded on every call. Kuramoto__N6 is hit hardest (16.3%)
   because its per-Prune work (sin/cos, fewer empties) is short, making the fixed overhead
   relatively large. (Same bug in `contractor_ibex_polytope.cc`, fixed simultaneously.)
   **Fixed — see next section.**

4. **ExpressionEvaluator (Drake symbolic, 4.5–7%):** `EvaluateBox` evaluates the formula set
   via dReal's own `ExpressionEvaluator` (the Drake symbolic traversal) to decide
   delta-satisfiability. This is separate from IBEX's compiled `HC4Revise` path. The
   `VisitExpression` dispatch (4.2% leaf on xwin1.5) + hash-table variable lookups (1.4%) + the
   accumulate-over-coefficients path in `VisitAddition` (1%) are the subcomponents. This is the
   next addressable overhead after the timer fix.

5. **Allocation (5%):** `_xzm_free` at 2% + `IntervalVector::IntervalVector` (copy constructor)
   at ~0.5% + other malloc/free. IntervalVector copies in HC4Revise's local workspaces.

6. **Branching (0.4%):** `FindMaxDiam` / `BranchLargestFirst` is negligible. The planned
   branching-heuristic A/B is **ruled out** — there is no meaningful headroom here.

## odeexpr: IcpStat timer gates fixed (2026-06-20)

**Root cause.** `ContractorIbexFwdbwdStat` (and identical pattern in
`ContractorIbexPolytopeStat`) called `stat.timer_pruning_.resume()` and
`stat.timer_pruning_.pause()` directly, without gating on `stat.enabled()`. With the default
spdlog level `off`, `stat.enabled() = false` but the two `steady_clock::now()` calls per
`Prune` still fired, spending 4–16% of runtime computing a timing value that was never read.

**Fix (2 files).** Gate the calls:

```cpp
// before
stat.timer_pruning_.resume();
// ... prune ...
stat.timer_pruning_.pause();

// after
if (stat.enabled()) stat.timer_pruning_.resume();
// ... prune ...
if (stat.enabled()) stat.timer_pruning_.pause();
```

Applied in `src/dreal/contractor/contractor_ibex_fwdbwd.cc` (lines 100, 133) and
`src/dreal/contractor/contractor_ibex_polytope.cc` (lines 155, 157). The `icp_seq.cc` and
`icp_parallel.cc` timer calls are already correctly gated via `TimerGuard(…, stat.enabled(), …)`
— no change needed there.

**Verification.** Post-fix `sample` on kuramoto__N6: `mach_continuous_time` drops from the
#1 leaf (11.3%) to **0.0%** — not a single sample lands in the timer path. Timer information
is still collected and printed when `--verbose 2` or higher is passed (spdlog info level enables
`stat.enabled() = true`). Rounding debug gate: PASS (no rounding-mode assertion fired).

**Measured impact.** Profile-confirmed 16.3% → 0% on kuramoto__N6. One-trial spot check on
kuramoto__N5: **88.3 s CPU** (pre-fix baseline reference: 101 s), ~13% faster — consistent
with the 16.3% timer share given single-trial variance and that N5/N6 benchmarks were slightly
regenerated (different hash). The 4.1% share on tanh-heavy benchmarks (xwin1.5, J0.6) yields
a smaller but real gain there. `/benchmark` regression check: 9 ran, 0 new anomalies; the only
flagged item (`tacas_k7_UNS`) is a pre-existing delta-boundary near-sat issue already tracked
in `state.json` before this session — unrelated to this fix.

---

## Open avenues (deferred — post-timer-fix)

These ideas are not yet attempted. Ordered by estimated confidence × effort.

### A. ExpressionEvaluator overhead (medium confidence, medium effort)

**What:** `EvaluateBox` calls dReal's own `ExpressionEvaluator` (the Drake symbolic traversal)
once per formula after every successful `Prune`, to check delta-satisfiability. This costs
4.5–7% of total runtime (profile above). Components: `VisitExpression` dispatch, hash-table
variable-index lookups (`__hash_table::__emplace_unique`, ~1.4%), and the coefficient
accumulate loop in `VisitAddition`.

**Direction:** The hash-table lookup is a `map<Variable, int>` index lookup done per
variable per eval. A flat sorted-vector or pre-built index array could replace it.
Alternatively, if the formula set is stable across ICP iterations (it is — it's set once),
precompiling the formula evaluators into IBEX `Function` objects (which already do CSE and
compile to a flat byte stream) and reusing the HC4 forward-eval path would eliminate the
Drake traversal entirely. That is a larger restructuring (the `FormulaEvaluator` and
`ExpressionEvaluator` classes are the eval layer).

**Caution:** `EvaluateBox` also determines which formulas are violated (the `DynamicBitset`
returned) — branching uses this. Any replacement must preserve that output.

### B. Allocation reduction — IntervalVector copies in HC4 (medium confidence, medium effort)

**What:** `IntervalVector::IntervalVector` (copy constructor) and `_xzm_free` collectively ~5%
of runtime. The copy constructor appears in the HC4 forward pass (allocating the input vector
for each `Eval::eval` call) and in the backward pass's local snapshots. Post-Phase-2,
exception-elimination removed the `try/catch` frame but may not have changed heap turnover in
the backward engine's local allocations.

**Direction:** Instrument IBEX's `eval` and backward paths with Instruments → Allocations
(or `malloc_count`) to confirm the allocation sites and count. If `IntervalVector` copies are
O(constraints × ICP-iterations), a preallocated workspace (thread-local or per-contractor)
that is resized-once and reused across calls could eliminate the per-call allocation.

**Note:** This is in IBEX's core (`ibex::Eval`, `CompiledFunction`), so the change would live
in the ibex-fork, following the same pattern as the exception-elimination patches.

### C. Per-constraint skip-if-unchanged gate (low confidence, medium effort)

**What:** IBEX's `ContractorFixpoint` iterates all N constraints until no box shrinks. For
odeexpr's Lyapunov formulas, many constraints involve non-overlapping variable clusters; a
narrowing in constraint `i` rarely propagates to constraint `j` unless they share a variable.
A dependency graph that tracks which variables each constraint reads/writes could gate
re-evaluation of constraint `j` until one of its input variables changes.

**Why low confidence:** The `--worklist-fixpoint` flag (a coarser version of this idea)
was measured net-negative on both ODE and odeexpr families — faster on SAT-easy instances
but catastrophically slow on UNSAT-difficult ones (k17 23 s → 0.85 s; k70 47 s → 7876 s).
A per-constraint graph rather than a global queue reorder might have better variance, but
that distinction is unproven. Profile above shows HC4 forward (13.6%) + backward (12%) = 25%
— so if this idea works, there is meaningful headroom. Worth a targeted A/B only if the
worklist-fixpoint catastrophe can be traced to the global reorder rather than the early-exit
logic.

### D. Constraint ordering heuristic (low confidence, low effort)

**What:** IBEX evaluates constraints in declaration order (as they appear in the parsed
`.smt2`). A heuristic that front-loads high-shrinkage constraints might cut fixpoint
iterations. The profile shows `HC4Revise::proj` (the per-constraint iteration entry point)
at ~1.6% leaf — the overhead of cycling through low-yield constraints is embedded in
`CompiledFunction::forward/backward`. Reordering is a one-time setup cost.

**How to A/B:** Read the current constraint order from `ContractorFixpoint`'s contractor list
at solve start, sort by some heuristic (e.g. number of variables, or by profiling iteration
zero's shrinkage), then run. This is dReal-side and does not require ibex-fork changes.

### E. Gaol Lever 3 — 1 toggle per transcendental (low confidence, high effort)

**What:** After Levers 1 (inline FPCR write) and 2 (batch dn_up pairs → 2 toggles per
transcendental), the remaining gaol fesetround cost is the essential 2-per-transcendental
directed-rounding computation. A "Lever 3" would compute both the lower and upper bound in a
single upward-mode pass using the identity `lb = -round_up(-f(x))`, eliminating the
nearest→upward toggle and leaving only the upward→nearest restore — 1 toggle per
transcendental instead of 2.

**Why high effort / low confidence:** Requires restructuring gaol's `cos/sin/tanh/exp/atanh`
inner bodies to use the negation trick for the lower bound rather than a separate
downward-mode call. Each transcendental's correctly-rounded bound computation is non-trivial
(gaol uses range-reduction + polynomial approximation with error bounds). Verification would
need the same 132-case adversarial grid used for Levers 1 and 2 (see `gaol_transcendental_bitidentity_test.cc`),
extended to cover the Lever-3 form. The payoff is at most the ~15% remaining fesetround share
on the currently-solvable benchmarks (already reduced from 30–44% by Levers 1+2 and the
Phase-2 exit-path elimination). This is the last gaol-internal lever and should be attempted
only if avenues A–D are exhausted.
