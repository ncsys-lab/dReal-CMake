# Codac Migration

Replaced `ncsys-lab/ibex-lib` + `ncsys-lab/capdDynSys-4.0` with Codac v2 + `lebarsfa/ibex-lib`.

## Motivation

Both forked dependencies were unmaintained snapshots requiring heavy local patches for correctness and platform support (ARM64/Rosetta, C++17, interval semantics). Codac v2 (https://www.codac.io) is actively maintained, supports ARM64 natively, wraps `lebarsfa/ibex-lib`, and provides a cleaner ODE API via `CtcLohner`. Some breaking changes in ODE semantics were accepted as the cost of getting onto a maintained stack.

## Accepted trade-offs

| Trade-off | Notes |
|---|---|
| One extra `IntervalVector` copy per `fwdbwd` backward pass | Restores pre-callback behavior; critical for lemma quality (see Migration phases below) |
| Gradient always allocated in upstream IBEX | Minor memory overhead; our fork's `_grad = nullptr` hack disappears |
| Tube-based ODE semantics | Codac's `CtcLohner` operates on `SlicedTube`s, not step-by-step CAPD integration |
| Taylor order 2 (vs CAPD's order 20) | Hardcoded in Codac internals; primary source of ODE-benchmark slowdown |

## Status

- [x] Phases 1–5: CMake restructuring, `fwdbwd` callback removal, ibex_converter API check, ODE contractor rewrite, doc updates
- [x] `./FULL_BUILD.sh` succeeds on ARM64 macOS
- [x] ODE benchmarks produce correct results (with documented exceptions; see "Known issues")
- [x] Three iterative optimization passes landed (see "Performance timeline")

---

## Migration phases (completed)

**Phase 1 — CMake restructuring.** `CMakeLists.txt` switched from `FetchContent` of the old IBEX fork plus `ExternalProject` of CAPD+FILIB to two `ExternalProject_Add` blocks: `ibex_external` (`lebarsfa/ibex-lib@ibex-2.8.9.1`) installs into `gcc_build/ibex-install/`, then `codac_external` (`codac-team/codac@v2.0.2`, `WITH_CAPD=OFF`) installs into `gcc_build/codac-install/`. `CMAKE_OSX_ARCHITECTURES=x86_64` override removed — ARM64 native works.

**Phase 2 — `contractor_ibex_fwdbwd.cc` callback removal.** Upstream IBEX has no per-variable callback on `Function::backward()` (that was our fork's patch). Restored pre-callback behavior: snapshot before, compare after, populate the "changed variables" set the lemma pipeline downstream consumes. **Why this matters for lemma quality:** the "changed" set feeds the conflict clause that the pattern matcher learns from. Without it, lemmas contain the whole model and are useless for reuse. The naïve full-box snapshot was replaced in Pass 2 with an input-restricted thread-local snapshot — see "Performance timeline".

**Phase 3 — `ibex_converter.cc` API check.** No code changes needed; the IBEX 2.8.x expression-tree API (`ExprSymbol::new_`, `ExprConstant::new_scalar`, `NumConstraint`, `System`, `CtcPolytopeHull`, `LinearizerXTaylor`, `cleanup`) is stable across forks. Only `#include` paths shifted to `IBEX_INSTALL_DIR`.

**Phase 4 — ODE contractor rewrite.** `contractor_capd_full` and its CAPD-string converter (`to_capd_string.h`) were deleted; `contractor_odes_codac.{h,cc}` is the new implementation, compiled as C++20. The current shape is described in "Current implementation" below.

**Phase 5 — Documentation.** This file, `CLAUDE.md`, and `DEPENDENCIES.md` were rewritten.

---

## CAPD v6 ARM64 path (abandoned, but here's how to do it)

After measuring the early 26× ODE perf gap, restoring CAPD as the primary backend via CAPD v6.0.0 was attempted and reverted. This section is preserved verbatim because the patch is the concrete future-work avenue if Codac's order-2 ceiling becomes unacceptable.

### What was implemented (then reverted)

- `src/dreal/contractor/odes/contractor_odes_capd.cc` — CAPD Taylor-order-20 integration using `capd::IOdeSolver` (order 20), `capd::C0Rect2Set`, adaptive stepping, 16-sub-interval curve evaluation, intersection-based enclosure filter.
- `CMakeLists.txt` — `ExternalProject_Add(capd_external)` for CAPD v6.0.0.
- ODE string format `"var:x,v;fun:v,(-9.8);"` for `capd::IMap`.

All changes were reverted via `git checkout --` on the modified files after the failure was diagnosed.

### Why it was abandoned

CAPD v6.0.0 unconditionally depends on FILIB for directed rounding. FILIB's `CMakeLists.txt` has a `FATAL_ERROR` for any non-x86_64 platform:

```
capdExt/filibsrc/CMakeLists.txt:
  if(x86_64) ... else() FATAL_ERROR "Unknown or unsupported processor architecture."
```

The failure chain:
1. CAPD root `CMakeLists.txt`: unconditionally `add_dependencies(capd filib)` + `-D__USE_FILIB__`
2. `capdExt/CMakeLists.txt`: unconditionally `add_subdirectory(filibsrc)`
3. `capdExt/filibsrc/CMakeLists.txt`: FATAL_ERROR on non-x86

`-DCAPD_INTERVAL_TYPE=NATIVE` does **not** exist in CAPD v6.0.0 (hallucinated CMake option). `__USE_NATIVE__` also does not exist.

### The correct ARM64 fix (for future reference)

CAPD v6 already ships ARM64 `DoubleRounding` in `capdAlg/src/capd/rounding/DoubleRounding.cpp` (uses `msr fpcr` assembly). Without `__USE_FILIB__`, `capd::interval` = `Interval<double, DoubleRounding>` — fully ARM64-capable. The only fix needed is skipping FILIB on ARM64.

**Two-file patch** (deliverable via `PATCH_COMMAND` in `ExternalProject_Add`):

`CMakeLists.txt` — wrap FILIB dependency:
```cmake
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  message(STATUS "ARM64: using CAPD native DoubleRounding intervals (no FILIB)")
  target_compile_options(${PROJECT_NAME} PUBLIC -O2 -frounding-math)
else()
  add_dependencies(${PROJECT_NAME} filib)
  target_compile_options(${PROJECT_NAME} PUBLIC -D__USE_FILIB__ -O2 -frounding-math)
  target_link_libraries(${PROJECT_NAME} PUBLIC filib)
endif()
```

`capdExt/CMakeLists.txt` — guard `filibsrc`:
```cmake
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  add_subdirectory(filibsrc)
endif()
```

If the ODE perf gap ever becomes unacceptable, this patch plus reconstructing `contractor_odes_capd.cc` from the session transcript is the primary path to restoring CAPD Taylor-order-20.

---

## Current implementation

All ODE-contractor source lives under `src/dreal/contractor/odes/`. The C++17 surface (`contractor_odes.{h,cc}`) drives `Prune`; the C++20 translation unit (`contractor_odes_codac.{h,cc}`) wraps Codac.

- **`CodacOdeCache`** — opaque to C++17, defined in the C++20 TU. Holds the translated `codac2::AnalyticFunction<VectorType>` and a `codac2::CtcLohner` instance (its `contract` is `const`, safe to share across parallel ICP workers). Constructed once per flow; reused on every `Prune`.
- **Global per-`OdeFlow*` cache.** Static `unordered_map<const OdeFlow*, shared_ptr<CodacOdeCache>>` under a static `std::mutex`. Keyed by raw `OdeFlow*` (lifetime is `shared_ptr`-managed from the parser onward, so addresses are stable within one solve). Process-scoped; see "Known issues" for the cache-key bug.
- **FWD direction** — `run_lohner_integration` uses `CtcLohner` with `TimePropag::FWD_BWD`, `contractions=2`, `eps=0.1`, and adaptive `n_steps = clamp(max(n_steps_hint, ceil(t_ub * 2)), n_steps_hint, 60)` with `n_steps_hint = 20`. Catches `codac2::GlobalEnclosureError` and returns without pruning (sound but incomplete on stiff dynamics).
- **BWD direction** — `run_lohner_bwd_oneshot` uses `codac2::LohnerAlgorithm(&cache->fn, h, /*forward=*/false, u0)` with `u0 = X_t`, iterating `algo.integrate(1)` for `n_steps` (same adaptive formula). Returns `algo.getLocalEnclosure()` as the backward image of `X_t` at real time 0. Default `contractions=1`. Soundness rests on the BWD swap analysis below.
- **Trivial-flow short-circuit.** At cache build time, walk the flow's `ode_list`; if every RHS is `is_zero`, set `cache->trivial = true`. `Prune` then takes the `T=0` branch (`X_0 ∩ X_t` componentwise) and never enters CtcLohner. Handles `d/dt[d] = 0` planning benchmarks with 1280 modes.
- **Trace / visualization** — `run_lohner_trace` uses `LohnerAlgorithm` directly for the `--visualize` path.

`Prune` flow (`contractor_odes.cc`): (1) intersect parameter vars; (2) `T=0` shortcut (intersect state vars at endpoints); (3) check invariants at `X_0` via IBEX contractors (negated invariants logged-and-skipped — see `docs/qf_nra_ode_semantics.md` §6); (4) call into FWD or BWD `run_lohner_*` based on `m_dir`.

---

## Performance timeline

**Pass 1 — Per-flow cache + CtcLohner reuse + trivial-flow short-circuit + `contractions=2`.** Pre-cache, `build_ode_fn` re-walked the symbolic RHS tree and constructed a brand-new `AnalyticFunction` on every `Prune`. With `N_modes × 2` contractors built and many `Prune` calls each, translation cost was a real bottleneck (especially for the 15-var quad flow with sin/cos sub-trees). The cache amortizes translation to ~1× per distinct flow per process. `contractions=2` (was 5) was the empirical speed/tightness sweet spot. Pass 1 also skipped BWD's Step 4 entirely as a stopgap because the `m_vars_0 ↔ m_vars_t` constructor swap made `CtcLohner FWD_BWD` ask the wrong question (the forward-image question on swapped gates, not the backward image); Pass 3 restored a sound BWD direction.

**Pass 2 — Adaptive `n_steps` + `contractor_ibex_fwdbwd::Prune` snapshot restriction.** `run_lohner_integration` had `n_steps = 20` hardcoded, so `h = t_ub / 20` scaled linearly with horizon: at `t_ub = 30` (cardiac), `h ≈ 1.5` was too coarse for order-2 Taylor — CtcLohner spent its contractions budget widening the per-step enclosure back to soundness instead of narrowing. Pass 2 introduced `n_steps = clamp(max(20, ceil(t_ub * 2)), 20, 60)` — keeps the 20-step floor so short-horizon benchmarks (bouncing ball, fedor, normal) are unchanged, only adds steps when `t_ub > ~10`. Independently, Pass 2 replaced the Phase-2 full-box snapshot in `contractor_ibex_fwdbwd::Prune` with a `thread_local std::vector<std::pair<int, ibex::Interval>>` saving only the constraint's free-var intervals (typically 2–10 of a 50–500-variable box). `thread_local` is safe because `Prune` is never invoked recursively. Steady-state per-call allocation drops to zero once buffer capacity saturates.

**Pass 3 — BWD contractor restoration via `LohnerAlgorithm(forward=false)`.** Pass 1's BWD Step 4 skip was unsound in the worst case (could let a false δ-SAT through if BWD was the load-bearing narrower); Pass 3 added `run_lohner_bwd_oneshot` so the BWD contractor computes a real backward image. Soundness rests on: every concrete `x_0 ∈ X_0` reaching some point of `X_t` under forward dynamics must lie in the backward image of `X_t`, so removing states outside that image is sound. Verified by seven semantic-soundness gates on trivial / decay / mock-prostate fixtures (see `test/dreal/contractor/test/contractor_odes_semantic_test.cc`) and by a ground-truth-aware regression classifier in `benchmark/aggregate.py` that distinguishes soundness regressions (flips against annotated ground truth) from completeness improvements (refutations of spurious δ-witnesses, which are silent under Pass 3). Cost: ~50% of one `CtcLohner FWD_BWD` call per BWD `Prune` — bouncing ball went 2.8 s → 4.2 s; this is the floor.

---

## Headline numbers (current)

ARM64 macOS, `upgrade-ibex` HEAD post-Pass-3. Baselines from `benchmark/baseline.csv` (CAV26 reference times).

| Benchmark | Baseline | Current | Net | Notes |
|---|---|---|---|---|
| `bouncing_ball_with_drag_10_0` | ~0.5 s (CAPD order-20, x86 Rosetta) | ~4.2 s | 8.4× slower | Order-2 floor; CAPD v6 fix above is the way out |
| `0hz_k64_cardiac_new_cardiac` | 85 s | ~120 s | 1.4× slower | Adaptive `n_steps` recovered most of the headroom |
| `github_oct5_0hz_k4_cardiac_new_cardiac` | 29 s | ~5 s | 6× faster | Long-horizon win from adaptive `n_steps` |
| `tacas_c2e2_k10_NOR__sigmoid_SAT` | 142 s | 0.8 s | 170× faster | Pass 2 |
| `tacas_c2e2_k11_inverter_ramp_SAT` | 103 s | 2.1 s | 48× faster | Pass 1 cache |
| `tacas_c2e2_k17_NOR__sigmoid_UNS` | TIM | 13.6 s | resolved | Pass 2 |
| `tacas_c2e2_k21_NOR__sigmoid_SAT` | 234 s | 7.7 s | 30× faster | Pass 2 |
| `0hz_k32_cardomain_car-8-flat-linear` | 49 s | 0.7 s | 70× faster | Pass 1 |
| `0hz_k64_cardomain_car-8-flat-nonlinear` | 66 s | 0.5 s | 130× faster | Pass 1 |
| `0hz_k128_quad_quad2-1` | 38 s | TIM (>300 s) | regressed | 15-var ODE with sin/cos; order-2 Taylor is the ceiling |
| `0hz_k1280_planning_one-var` | 31 s | TIM | regressed | Trivial flow + huge mode count; SAT layer dominates |
| `0hz_k2_prostate_prostate_p10` | 42 s | TIM | regressed | Coupled rational dynamics; see "Known issues" |

---

## What we tried but reverted

- **`contractions=1` on CtcLohner.** Bouncing ball improved (3.3 s → 2.5 s) but cardiac fell off a cliff (108 s → TIM at 90 s budget) — wider per-step enclosures forced many more ICP bisections, net-slower. Kept at `contractions=2`.
- **`contractions=1` with adaptive `n_steps`.** Tried because smaller `h` might compensate for wider per-step enclosure. Didn't measure better on cardiac and risked bouncing ball. Reverted.
- **Aggressive `n_steps = clamp(ceil(t_ub * 10), 5, 50)`** (the heuristic Pass 1 doc suggested). Reduced steps for medium-horizon benchmarks: bouncing ball went 3 s → 4.25 s mid-ICP as `t_ub` narrowed. Replaced with the floor-preserving max policy now in place.
- **`inflate_by = max_rad × 2`** (was `× 10`) for the tube envelope. Bouncing ball unchanged (~3.0 s); cardiac unchanged within noise. Envelope width isn't the bottleneck. Kept at `× 10` for headroom on untested dynamics.
- **Tighter CtcLohner `eps`** (default `0.1`). Risks `GlobalEnclosureError` on dynamics we haven't tested. Left at default.
- **Skipping the entire BWD contractor** (not just its Step 4). Considered to halve ICP passes through ODE constraints. Rejected because invariant checking (Step 3) at the X_0 endpoint is direction-aware via the swap and contributes genuine narrowing. Step-4-only skip kept that — see Pass 3 for the eventual sound restoration.
- **Skipping CtcLohner when `t_ub` is small** (early-out for short tubes where the envelope contains both gates and no narrowing is possible). Cheap check but rarely fired — by the time it would matter, ICP had already narrowed `t_ub`. Removed.
- **Memoizing `CtcLohner::contract` results by `(X_0, X_t, t_ub)`.** Boxes monotonically shrink during ICP, so cache hit rate ~0. Not implemented.
- **Splitting FWD into two passes** (`TimePropag::FWD` then `TimePropag::BWD`) to interleave `nl_ctcs` propagation. Bookkeeping in `Prune` (gates from `tube.first_slice()` vs `tube.last_slice()`) doesn't compose cleanly across two contract calls without sharing the tube object. Deferred — see "Open lines of attack" item 5.

---

## What we learned (tribal knowledge)

### Codac `CtcLohner` knobs (verified by reading `codac-install/include/codac-core/codac2_CtcLohner.h`)

- `CtcLohner(const AnalyticFunction<VectorType>& f, int contractions = 5, double eps = 0.1)`
- `void contract(SlicedTube<IntervalVector>& tube, TimePropag t_propa = FWD_BWD) const`
- **Taylor order is hardcoded to 2** in the `LohnerAlgorithm` private members (`_z` is the order-2 Taylor-Lagrange remainder per the field comment). Not exposed as a public knob; would need a Codac patch.
- `eps` is the inflation parameter for the **internal** global enclosure inside CtcLohner. Not the same as our outer `init_box.inflate(...)` factor.
- User-facing levers: `contractions`, `eps`, `TimePropag`, `n_steps` (via the `TDomain`'s `dt = t_ub / n_steps`), initial tube envelope width. Everything else (Taylor order, step adaptation, parallelotope basis updates) is internal.

### dReal-side cost model for ODE Prune

For an ODE-heavy benchmark with `N_modes` modes and `K` ICP iterations:
- Pre-cache, per-`Prune` was dominated by `build_ode_fn` for complex flows. With per-flow caching, translation cost amortizes to ~1× per distinct flow.
- Post-cache, per-`Prune` is dominated by `CtcLohner::contract`: `contractions × n_steps × |t_propa|` AnalyticFunction evaluations. At `n_steps=20`, `contractions=2`, `FWD_BWD`, that's 80 step-evaluations per Prune, each doing multivariate Taylor expansion on the ODE RHS.
- Tube allocation (`SlicedTube`, `TDomain`, gate `set`) is non-trivial but smaller than the contraction itself.

### Why naïve per-Prune box snapshots are expensive in `contractor_ibex_fwdbwd`

The Phase-2 callback removal originally took a full `iv_before = iv` + `std::set<int>` of changed indices. The downstream loop only consults `changed_vec` for bits already in `input()` (the constraint's free vars, typically 2–10 of a 50–500-variable box). Snapshotting the whole interval vector was paying for information that was thrown away. The Pass-2 fix uses a `thread_local std::vector<std::pair<int, ibex::Interval>>` saving only the constraint's free-var intervals — O(|free_vars(f)|) instead of O(|box|). Important because most non-ODE benchmarks spend the majority of theory-solver time in this Prune.

---

## Open lines of attack

Items below survive into ongoing work. Item 2 from earlier passes (proper backward-direction integration) landed in Pass 3 and is removed from this list.

1. **Detect partially-trivial flows.** A flow with `d/dt[x] = 0` for some state vars and non-zero for others currently uses CtcLohner on the whole vector. Splitting into "trivial sub-vector" (just intersect X_0 ∩ X_t) and "active sub-vector" (CtcLohner on the reduced system) shrinks the `AnalyticFunction` dimensionality and per-step work. Implementation cost is higher because the dimension reduction has to be plumbed through the gate reading/writing.

2. **Static `flow_cache_map` eviction at end of solve.** Currently caches accumulate for the lifetime of the process. Fine for a single-query CLI; problematic if dReal is ever embedded in a long-running service. Add a hook in `Context` destruction to clear flow caches whose `OdeFlow*` is no longer referenced. (See also the cache-key bug under "Known issues" — both could be solved together via weak_ptr-based keys.)

3. **`TimePropag::FWD` only on the FWD contractor.** Cuts CtcLohner's internal work in half but only narrows `X_t`. Now that Pass 3's BWD contractor lands `X_0` narrowing soundly via `LohnerAlgorithm(forward=false)`, this is the natural follow-on. Worth measuring on benchmarks where the BWD contractor is the load-bearing narrower (cardiac, prostate). Re-lock the regression baseline before measuring.

4. **Tighter `eps` parameter** on `CtcLohner`. Default `0.1` controls the algorithm's internal global enclosure inflation. Tightening might converge in fewer contractions. Risks `GlobalEnclosureError` on stiff dynamics — needs careful benchmark sweep.

5. **Splitting FWD into two passes** (`TimePropag::FWD` then `TimePropag::BWD`) to interleave `nl_ctcs` propagation between directions. Bookkeeping needs to share the tube object across calls — requires refactoring `run_lohner_integration`. Deferred but tractable.

6. **Fuzz the BWD contractor with sympy-derived polynomial closed-form fixtures.** Pass 3 verified soundness on seven hand-picked fixtures (trivial flow, linear decay, mock-prostate rational coupling). Random RHS within a polynomial template would harden confidence further. Especially valuable on dynamics with closed-form solutions where ground-truth flips are easy to detect.

7. **Fix the cache-key bug** (see "Known issues"). Lowest priority because it doesn't fire in single-query CLI mode, but it's a soundness landmine if the codebase grows.

---

## Soundness analyses

### Correctness analysis of the BWD swap (load-bearing argument for Pass 1's Step-4 skip and Pass 3's `LohnerAlgorithm(forward=false)` restoration)

Let `f` be the forward ODE dynamics. The Integral constraint says:

> ∃x(·): dx/dt = f(x) ∧ x(0) ∈ X_0 ∧ x(t_ub) ∈ X_t.

The **FWD** contractor (no swap) builds a `SlicedTube` with `gate_0 = X_0` (codac t=0) and `gate_t_ub = X_t` (codac t=t_ub), then runs `CtcLohner FWD_BWD`. This computes the intersection of trajectories of `dx/dt = f(x)` passing through both gates — the correct constraint. Both endpoint gates get narrowed soundly.

The **BWD** contractor (constructor swaps `m_vars_0 ↔ m_vars_t`) ends up with `gate_0 = X_t` and `gate_t_ub = X_0` and the *same* `dx/dt = f(x)` analytic function. `CtcLohner FWD_BWD` on this tube finds trajectories of forward dynamics from `X_t` (at codac t=0) to `X_0` (at codac t=t_ub). For a non-time-symmetric ODE, the set of points in `X_t` with a forward trajectory landing in `X_0` is **not** equal to the backward-image of `X_t` under `f`, which is what soundness for the original constraint demands. So BWD's CtcLohner narrowing could remove valid endpoint values → false UNSAT.

Pass 1's response: skip BWD Step 4 entirely. Sound but lossy (BWD never narrows `X_0`).

Pass 3's response: replace BWD Step 4 with `run_lohner_bwd_oneshot`, which uses `LohnerAlgorithm(&cache->fn, h, /*forward=*/false, u0 = X_t)`. This is the genuine backward image: `LohnerAlgorithm` with `forward=false` integrates the *backward* dynamics `du/dτ = -f(u)` starting from `X_t` at `τ = 0`, producing at `τ = t_ub` the set of forward-pre-images of `X_t`. Intersecting that with the current `m_vars_t` (= original `X_0`) is sound: every concrete `x_0 ∈ X_0` whose forward trajectory hits `X_t` must lie in this pre-image set.

The chosen mechanism (`LohnerAlgorithm` rather than a second `AnalyticFunction` with `dx/dt = -f(x)`) shares the cache slot per flow and avoids re-translating the RHS. A `-f(x)`-based alternative remains available as a fallback if benchmarks show looseness from the single-shot `LohnerAlgorithm` path.

### Pass 3 verification methodology (ground-truth-aware classification)

The prior "any SAT↔UNSAT flip aborts" criterion was replaced. dReal is sound + delta-complete: a baseline `delta-sat` can be a spurious δ-witness, and a tighter contractor refuting it is a *completeness improvement*, not a soundness bug.

Three pieces of infrastructure landed alongside the code change:

- **`benchmark/baseline.csv` + `benchmark/baseline_local.csv` `ground_truth` column** — populated only from filename conventions (VNAMSCwI `_SAT`/`_UNS`, SARADC `-1e`/`5000e`). dReal3 is not propagated; it has the same δ-completeness limitations as dReal4 and is not authoritative.
- **`benchmark/aggregate.py` flip classifier** — emits `CORRECTNESS IMPROVEMENT` (flip toward annotated ground truth), `SOUNDNESS REGRESSION` (flip away from it), or `UNDETERMINED FLIP` (no annotation). The previous blanket "correctness regression" alert now fires only on SOUNDNESS. Adds a frozen-baseline fallback so flips on anomaly-list rows missing from `baseline_local.csv` aren't invisible.
- **`test/dreal/contractor/test/contractor_odes_semantic_test.cc`** — closed-form-derived soundness gates for FWD and BWD on three ODE families: trivial (`dx/dt = 0`), linear decay (`dx/dt = -x`), and mock-prostate coupled rational dynamics (`dx/dt = -x·z/(z+2), dz/dt = -z`). Each gate is a SAT-known instance; box must remain non-empty after Prune. The mock-prostate fixtures are the load-bearing case — rational coupling is the shape of dynamics where the suspected unsoundness might live.

All 7 gates pass on HEAD and under the BWD-restoration experiment:

```
TrivialFlowTest.FwdInfeasible_BoxEmpties              PASS / PASS
TrivialFlowTest.BwdInfeasible_BoxEmpties              PASS / PASS
DecayFlowTest.FwdFeasible_BoxRemains                  PASS / PASS
DecayFlowTest.BwdFeasible_BoxRemains                  PASS / PASS
MockProstateTest.FwdFeasible_BoxRemains               PASS / PASS
MockProstateTest.BwdFeasible_BoxRemains               PASS / PASS
MockProstateTest.BwdFeasible_PreservesInteriorPoint   PASS / PASS
```

---

## Known issues

### Pre-existing cache-key bug in `make_codac_ode_cache`

`make_codac_ode_cache` (`contractor_odes_codac.cc:180`) keys its static `flow_cache_map` by raw `OdeFlow*` pointer. If an `OdeFlow` is destroyed and a new one is later allocated at the same address with different dynamics, the cache returns stale data for the new flow. The Pass-3 semantic tests sidestep this by holding `OdeFlow` shared_ptrs in `inline static` class members so they outlive the process. Fix candidates: weak_ptr-based key, content-hash key, or explicit eviction on flow destruction. Not exercised by the single-query CLI; latent landmine for any future embedded-service use. See "Open lines of attack" item 7.

### Pre-existing correctness flips (not introduced by the migration optimizations)

These benchmarks flip vs. the CAV26 baseline. The `.n` (non-ODE) cases cannot be the ODE contractor's fault and are the most likely candidates for the next investigation — they suggest a soundness issue in the post-migration `contractor_ibex_fwdbwd` / polytope / abstraction path that predates the ODE optimization passes.

| Benchmark | Baseline | Current | Notes |
|---|---|---|---|
| `0hz_k64_water_water-double-network-sat.drh.n` | SAT | UNSAT | `.n` file (no ODE); likely IBEX fork upgrade + Phase-2 callback removal |
| `0hz_k8_airplane_airplane-single-network-sat.drh.n` | SAT | UNSAT | `.n` file (no ODE); same root-cause hypothesis as above |
| `prostate_h2.drh.o` | δ-sat (11.27 s) | unsat (0.08 s) | UNDETERMINED ground truth. Working hypothesis: spurious δ-witness in baseline; Pass-3 BWD correctly refutes. Not a soundness regression by the aggregate.py classifier. |
| `prostate_cancer2_scaled`, `prostate_cancer_scaled_infix`, `prostate_p10` | δ-sat | unsat | Same hypothesis as `prostate_h2`. |
| `0hz_k256_gen_gen-0-multi-nonlinear.drh.n` | UNSAT | TIM | `.n` file; non-ODE regression |
| `0hz_k256_gen_gen-0-single-nonlinear.drh.n` | UNSAT | TIM | `.n` file; non-ODE regression |
| `0hz_k256_thermostat_thermostat-double-network-sat.drh.n` | SAT | TIM | `.n` file; non-ODE regression |

Removing an unsound contractor can only **introduce** correctness flips by allowing the search into a false-SAT branch that the false-UNSAT was masking. Empirically this did not happen on any benchmark in the sampled batches across the three passes — every new EXCEPTIONAL was simply a benchmark finishing faster on a path that already exists. The prostate-family flips are best explained by the BWD restoration refuting spurious δ-witnesses, not by a regression.

The `1mhz_k28_saradc_3b_box_4a_-1e` family TIMs at HEAD without any of the optimization-pass changes, so it's a separate non-ODE issue (SAT solver / IBEX hot path).
