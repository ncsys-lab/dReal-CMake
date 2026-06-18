# dReal4 Optimization Log (`upgrade-ibex-opts`)

Iterative profile → optimize → benchmark loop. Each idea lands in **Adopted** (committed)
or **Rejected** (with numbers + reason). Acceptance bar: net PAR2 improvement ≈≥10% on the
probe + gate sets, **zero** SAT/UNSAT correctness flips vs ground truth, no >1.5× regressions.
Soundness guardrails (FE_UPWARD/FE_TONEAREST guards, no silent fallbacks) are non-negotiable.

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
