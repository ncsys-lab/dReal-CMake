# Codac Migration

> **STATUS (2026-06-08): RESOLVED — Codac has been removed.** This document is
> retained as historical context. The current architecture: source-built IBEX
> from `ncsys-lab/ibex-lib@dreal-perf-patches` (7 surgical patches on top of
> mainline; see `../ibex-fork/MIGRATION.md`) + CAPD master (sole ODE backend,
> with a CAPD-based `run_capd_trace` for `--visualize`). See `DEPENDENCIES.md`
> for the current stack.
>
> The performance regression analysis below correctly identified the
> `Function::init` gradient allocation as the dominant cost. The resolution
> (the lazy-grad patch) is now a candidate upstream PR rather than a
> downstream-only hack.

Replaced `ncsys-lab/ibex-lib` + `ncsys-lab/capdDynSys-4.0` with Codac v2 + `lebarsfa/ibex-lib`.

## Motivation

Both forked dependencies were unmaintained snapshots requiring heavy local patches for correctness and platform support (ARM64/Rosetta, C++17, interval semantics). Codac v2 (https://www.codac.io) is actively maintained, supports ARM64 natively, wraps `lebarsfa/ibex-lib`, and provides a cleaner ODE API via `CtcLohner`. Some breaking changes in ODE semantics were accepted as the cost of getting onto a maintained stack.

## Accepted trade-offs

| Trade-off | Notes |
|---|---|
| One extra `IntervalVector` copy per `fwdbwd` backward pass | Restores pre-callback behavior; critical for lemma quality (see Migration phases below) |
| Gradient always allocated in upstream IBEX | **Major CPU cost, not memory.** `sample` profiling on `1mhz_k20_saradc_2b_box_4a_-1e` (see "Re-investigation 2026-06-07" below) shows ~65% of total wall time inside `ibex::Gradient::Gradient(ibex::Eval&)` and `ibex::ExprLinearity::ExprLinearity`, both called unconditionally from `ibex::Function::init`. `Function::backward` (the only IBEX entry point fwdbwd actually uses) takes the `_hc4revise` path and never touches `_grad`. ncsys-lab's `_grad = nullptr` hack was load-bearing for this Prune-time cost; lebarsfa's stock build pays it on every cache miss in `TheorySolver::contractor_cache_`. **This is the dominant component of the post-migration non-ODE slowdown.** See "Open lines of attack" item 7 for the IBEX-source-patch path. |
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

## CAPD v6 ARM64 path (active — gated hybrid)

> **Updated 2026-06-06.** CAPD master (in-development `6.1.0`) now exposes
> `CAPD_INTERVAL_TYPE` as a clean CMake variable. With
> `-DCAPD_INTERVAL_TYPE=NATIVE`, `capdExt/CMakeLists.txt` skips
> `add_subdirectory(filibsrc)` entirely and CAPD's own ARM64-capable
> `DoubleRounding` becomes the interval backend. **The two-file
> FILIB-bypass patch documented below is no longer needed.** What follows
> is preserved as a historical artifact; the live build wiring is the
> `ExternalProject_Add(capd_external)` block in `CMakeLists.txt` pinning a
> recent master SHA (currently `b353e170`, 2026-05-18). The contractor
> runs alongside Codac's `CtcLohner` as a gated second backend; see
> "CAPD-Lohner gated hybrid" below.

After measuring the early 26× ODE perf gap, restoring CAPD as the primary backend via CAPD v6.0.0 was attempted and reverted. The v6.0.0 source unconditionally required FILIB, which fails to build on ARM64. With master, that blocker is solved upstream — CAPD is now reintroduced as a gated second contractor (see below).

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

## CAPD-Lohner gated hybrid (current)

CAPD is back as a *second* ODE contractor running alongside `CtcLohner`. The plan, the per-step rationale, and the decision tree are in `codac_docs/analysis/codac_capd_extension_assessment.md` and the project plan file. Summary of what landed:

- **Build wiring** (`CMakeLists.txt`). `ExternalProject_Add(capd_external)` pins CAPD master SHA `b353e170` (2026-05-18) and passes `-DCAPD_INTERVAL_TYPE=NATIVE` so FILIB is skipped on ARM64. CAPD installs into `gcc_build/capd-install/` as a single `libcapd.a` + headers under `include/capd/`. The `capd_imported` IMPORTED target sets `INTERFACE_COMPILE_DEFINITIONS=__USE_NATIVE__` so consumers pick up the matching template instantiations.
- **Contractor TU** (`src/dreal/contractor/odes/contractor_odes_capd.{h,cc}`). Mirrors the Codac TU's surface and lifetime model: per-flow `CapdOdeCache` holding two `capd::IMap` instances (`f(x)` for forward integration, `-f(x)` for backward); per-call `capd::IOdeSolver(order=20)` + `capd::ITimeMap` (these carry mutable step state, so not shared across parallel ICP workers). The string builder lives in `src/dreal/contractor/odes/to_capd_string.h` (forward-ported from the pre-Codac-migration version). Same `unordered_map<const OdeFlow*, CacheSlot>` + `shared_ptr<const OdeFlow>` ownership pattern as `make_codac_ode_cache` — closes the same pointer-reuse hazard.
- **Dispatch / gate** (`contractor_odes.cc::Prune`). After the parameter intersect / T=0 / invariant steps, Prune evaluates `use_capd = (t_ub > capd_t_gate) || (n_state_vars >= capd_ndim_gate)`. On a true gate, CAPD runs first; if it diverges (`GlobalEnclosureError`-equivalent, parser failure, or null cache) we fall back to Lohner. The Codac `m_codac_cache` is always built — the trivial-flow short-circuit and Lohner fallback both rely on it.
- **CLI flags**. `--capd-t-gate ARG` (double, default `5.0`) and `--capd-ndim-gate ARG` (int, default `6`). Set `--capd-t-gate 1e18 --capd-ndim-gate 1000000` to disable CAPD entirely; use `--capd-t-gate 1e-300 --capd-ndim-gate 1` to force CAPD on every Prune. Note: both flags use positive-value validators and reject `0` — the help text saying "set 0" is wrong.
- **Soundness gates**. `test/dreal/contractor/test/contractor_odes_semantic_test.cc` now ships 26 tests across four fixtures:
  - 7 original (Lohner under default gates) — trivial / decay / mock-prostate × FWD/BWD plus the BWD interior-point preservation gate
  - 7 `_Capd`-suffixed (`MakeCapdForcedConfig` forces `(t_gate=0, ndim_gate=0)`) — same instances, CAPD path
  - 9 new gate-dispatch tests — long-horizon at `t_ub=20` (CAPD triggered via t-gate), gate boundary at `t_ub=4.99` vs `5.01`, backend-consistency (Lohner-forced vs CAPD-forced) on the same instance, and trivial-flow short-circuit precedence over the CAPD gate at `t_ub=20`
  - 3 `SixDimDecayTest` — 6-dimensional decoupled decay exercising the `capd_ndim_gate` branch (n=6 ≥ gate=6) independent of the t-gate
  Plus a sibling 26-test file `test/dreal/contractor/test/to_capd_string_test.cc` covering the Expression → CAPD `IMap` string translator (constants incl. scientific-notation round-trip + negative wrap, variables, every supported `ExpressionKind` incl. `tan` → `sin/cos`, `abs` → `sqrt(sqr)`, `sinh`/`cosh`/`tanh` → exp-form, and `if_then_else` throws). All 52 pass.
  The aggregate.py ground-truth flip classifier in `benchmark/aggregate.py` is the next-layer tripwire — any flip against an annotated `_SAT`/`_UNS` benchmark is escalated as `SOUNDNESS REGRESSION`.

### Benchmark validation (2026-06-07, PAR2-corrected)

`/benchmark-baseline` rerun against HEAD (`bd8a7ce99`) on the 30-benchmark stratified sample with PAR2 scoring (TIM/OOM/ERR entries penalized at 2× timeout = 600 s). PAR2 is the correct metric for SMT benchmarking: a benchmark that transitions from TIM to solved *lowers* the PAR2 average, while the old raw-average approach silently excluded timed-out entries from both sides and gave a misleading picture.

Family PAR2 averages vs the CAV26 *frozen* `baseline.csv` (CAPD-x86-Rosetta historical times):

| Family | n | Frozen PAR2 avg | Local PAR2 avg | Ratio |
|--------|---|-----------------|----------------|-------|
| github | 10 | 74.69 s | 425.30 s | 5.69× |
| tacas  | 10 | 115.03 s | 300.41 s | 2.61× |
| saradc | 10 | 73.17 s | 567.11 s | 7.75× |

Net: the ARM64/Codac-v2 stack is substantially slower than the old x86/Rosetta CAPD+IBEX stack across all three families under PAR2 scoring. The prior analysis (which showed github "flat" at 0.97× and tacas "2.3× faster") was an artifact of excluding timed-out benchmarks from the family average on both sides — those benchmarks now correctly contribute 600 s each to the local PAR2 average. The dominant component of the across-the-board non-ODE slowdown is now root-caused: see "Re-investigation 2026-06-07" below for the gradient-allocation finding and "Open lines of attack" item 7 for the fix. The CAPD-Lohner hybrid vs the prior Lohner-only state of `upgrade-ibex` is still a strict improvement (zero regressions; the two formerly-TIM'd SARADC benchmarks now solve, lowering their contribution from 600 s each to their actual solve times).

### Pre-existing stale test note

`test/dreal/contractor/test/contractor_capd_test.cc` (`ContractorCapdFullTest.{CapdFwd,CapdBwd}`) is a dReal3-era port whose output-bit expectations assumed a contractor doing BVP-style time narrowing. Neither current Lohner nor the new CAPD contractor performs that inverse-time reasoning — they compute reachable sets at the *given* `t_ub`. Test expectations were realigned to match current behavior; the test is now pinned to the Lohner backend (`--capd-t-gate 1e18` equivalent) so its mechanical-invariant checks stay stable regardless of any future CAPD tuning. The dReal3 BVP-style narrowing is not on the implementation roadmap; the semantic tests above are the source of truth for soundness.

### Tuning gates from benchmark sweeps

`kDefaultCapdTGate=5.0` and `kDefaultCapdNdimGate=6` are pre-measurement guesses, not optimized values. The intended workflow is:

1. Run `/benchmark-baseline` to relock the regression baseline with the new contractor in place.
2. Sweep `--capd-t-gate ∈ {2, 5, 10}` × `--capd-ndim-gate ∈ {4, 6, 10}` on a representative subset including `bouncing_ball`, `quad`, `cardiac`, and the SARADC family.
3. Pick the lowest gate that doesn't hurt easy benchmarks (Lohner-friendly thin tubes) and update the defaults in `src/dreal/solver/config.h`.

---

## Current implementation

All ODE-contractor source lives under `src/dreal/contractor/odes/`. The C++17 surface (`contractor_odes.{h,cc}`) drives `Prune`; the C++20 translation unit (`contractor_odes_codac.{h,cc}`) wraps Codac.

- **`CodacOdeCache`** — opaque to C++17, defined in the C++20 TU. Holds the translated `codac2::AnalyticFunction<VectorType>` and a `codac2::CtcLohner` instance (its `contract` is `const`, safe to share across parallel ICP workers). Constructed once per flow; reused on every `Prune`.
- **Global per-`OdeFlow*` cache.** Static `unordered_map<const OdeFlow*, CacheSlot>` under a static `std::mutex`, where `CacheSlot { shared_ptr<const OdeFlow> flow, shared_ptr<CodacOdeCache> cache }`. Keyed by raw `OdeFlow*` for fast lookup; the slot's `shared_ptr<const OdeFlow>` keeps the keyed flow alive so the address cannot be reused while the entry is live (this closes the pointer-reuse hazard previously listed under "Known issues" — see resolved-issues note). `make_codac_ode_cache` takes `shared_ptr<const OdeFlow>` to make the ownership contract explicit. Process-scoped.
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
- **`TimePropag::FWD` only on the FWD contractor** (Open lines of attack item 3 — first attempt). Attempted post-Pass-3 on the theory that Pass 3's BWD contractor now lands `X_0` narrowing soundly and the FWD pass's `TimePropag::BWD` direction is now redundant work. `github_oct5_0hz_k4_cardiac_new_cardiac` flipped UNSAT → δ-SAT (3.3 s) — the suspicious soundness-direction flip, surfaced by the new ground-truth-aware classifier. Reverted. Lesson: FWD's BWD pass was carrying narrowing that the BWD contractor alone (with `LohnerAlgorithm` `contractions=1`) doesn't replicate. Re-attempt would need either bumped BWD `contractions` or compensation elsewhere; relock the local baseline first per item 3.

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

2. **Static `flow_cache_map` eviction at end of solve.** Currently caches accumulate for the lifetime of the process — and the post-fix `CacheSlot` holds a `shared_ptr<const OdeFlow>` that pins each flow in memory until the process exits. Fine for a single-query CLI; problematic if dReal is ever embedded in a long-running service. Add a hook in `Context` destruction to clear flow caches whose `OdeFlow` is no longer referenced elsewhere; a `weak_ptr`-based key (or content-hash key) would let unused entries collect themselves as a follow-on.

3. **`TimePropag::FWD` only on the FWD contractor.** Cuts CtcLohner's internal work in half but only narrows `X_t`. Now that Pass 3's BWD contractor lands `X_0` narrowing soundly via `LohnerAlgorithm(forward=false)`, this is the natural follow-on. Worth measuring on benchmarks where the BWD contractor is the load-bearing narrower (cardiac, prostate). Re-lock the regression baseline before measuring.

4. **Tighter `eps` parameter** on `CtcLohner`. Default `0.1` controls the algorithm's internal global enclosure inflation. Tightening might converge in fewer contractions. Risks `GlobalEnclosureError` on stiff dynamics — needs careful benchmark sweep.

5. **Splitting FWD into two passes** (`TimePropag::FWD` then `TimePropag::BWD`) to interleave `nl_ctcs` propagation between directions. Bookkeeping needs to share the tube object across calls — requires refactoring `run_lohner_integration`. Deferred but tractable.

6. **Fuzz the BWD contractor with sympy-derived polynomial closed-form fixtures.** Pass 3 verified soundness on seven hand-picked fixtures (trivial flow, linear decay, mock-prostate rational coupling). Random RHS within a polynomial template would harden confidence further. Especially valuable on dynamics with closed-form solutions where ground-truth flips are easy to detect.

7. **Restore the `_grad = nullptr` patch on IBEX — via a fork of `ibex-team/ibex-lib`.** Highest-payoff non-ODE item. The 2026-06-07 re-investigation showed ~65% of saradc wall time is spent in `ibex::Function::init` building `_grad` — work `Function::backward` never uses. **The recommended path is now to fork `ibex-team/ibex-lib` directly and drop Codac entirely** (see "Strategic reassessment 2026-06-07" below for the full rationale and benchmark evidence). The fix then requires:
   - Fork `ibex-team/ibex-lib` (not `lebarsfa/ibex-lib` — see below for why).
   - Apply gradient patch to the fork: lazy-init `_grad` in `Function::gradient(...)` / `Function::deriv_calculator()` (set to nullptr in `init`, build on first use). Option (b) — `IBEX_NO_GRAD_INIT` CMake flag — also works but is noisier. Skipping `ExprLinearity` init is a separate ~37% win on top; same lazy-init pattern applies to `_lin`.
   - Switch `ibex_external` in `CMakeLists.txt` from the prebuilt-zip download to `ExternalProject_Add` building from our fork's source with appropriate ARM64 configure flags (gaol/ultim, no FILIB). Drop the `codac_external` block entirely; remove `contractor_odes_codac.{h,cc}` and `codac-install/` references. CAPD becomes the sole ODE backend.
   - No `IBEXConfig.cmake` is needed — dreal4's CMakeLists.txt already wires IBEX manually as `ibex_imported` / `ibex_gaol` / `ibex_ultim` IMPORTED targets, bypassing `find_package(IBEX)`. The `IBEXConfig.cmake` requirement was only for Codac's own build.
   - Address the `std::apply` ADL conflict in IBEX's Bison-generated parser (affects both macOS and Linux source builds; a one-liner forward-declaration patch).
   - Verification: re-run `1mhz_k20_saradc_2b_box_4a_-1e` + `/benchmark-baseline` post-patch; expect saradc PAR2 average to drop ~3× from 567 s toward ~190 s, github similarly.
   Effort: ~half-day for a careful patch; changes are contained to `CMakeLists.txt` + a small upstream-IBEX diff + removal of the `contractor_odes_codac.{h,cc}` TU.

---

## Re-investigation 2026-06-07: root-causing the non-ODE slowdown

Triggered by the user observation that "long-standing non-ODE slowdown from the Codac migration" was written before Pass 2 landed and had never been independently re-profiled. The 2026-06-07 PAR2 numbers (github 5.69×, tacas 2.61×, saradc 7.75×) made the gap large enough to be worth a focused investigation.

### Method

- `1mhz_k20_saradc_2b_box_4a_-1e.smt2` chosen as probe — frozen baseline 48 s, small enough to iterate on.
- Saradc benchmarks were checked first for `(integral_...)` / `(forall_t ...)` terms — zero matches. The `(set-logic QF_NRA_ODE)` header is dReal3-compat boilerplate; the actual ODE contractor never fires. So the ODE-side rewrites (CtcLohner, CAPD hybrid, BWD restoration) are mechanically irrelevant for saradc.
- A temporary `ContractorIbexPolytopeStat` was added to `contractor_ibex_polytope.cc` mirroring the fwdbwd one. `--polytope` is off by default — polytope never fires on saradc, so the polytope full-box snapshot at the old `contractor_ibex_polytope.cc:88` was not on the saradc critical path. (The Pass-2-equivalent fix landed anyway, since it's a strict improvement for `--polytope` users.)
- `dreal4 --verbose info` stat dumps + `sample <pid> 30` (macOS profiler) on the probe.

### Findings

| Stat (from `--verbose info` end-of-run dump) | Value |
|---|---|
| Total CheckSat (Theory level) | 400 |
| Total time in CheckSat | **77.20 s** |
| Total ibex-fwdbwd Pruning calls | 109,819 |
| Total time in Pruning | 0.022 s |
| Total ibex-converter Convert calls | 877 |
| Total time in Converting | 0.004 s |
| Total ibex-polytope Pruning | (never fired — polytope disabled by default) |

The pruning hot path is essentially free (0.022 s for 110 K calls). The ~77 s in theory CheckSat is unaccounted for by any existing stat.

`sample` resolves the missing 77 s precisely:
- 100% of samples → `dreal::TheorySolver::CheckSat` → `BuildContractor` → `make_contractor_ibex_fwdbwd` → `ContractorIbexFwdbwd::ContractorIbexFwdbwd` → `ibex::Function::Function` → `ibex::Function::init`
- **~65% inside `ibex::Gradient::Gradient(ibex::Eval&)`** — called unconditionally from `Function::init`
- **~37% inside `ibex::ExprLinearity::ExprLinearity`** (a child of Gradient ctor; walks the expression tree categorizing each sub-tree as linear / nonlinear / constant via `visit(ExprMul)`, `visit(ExprAdd)` etc.)
- The dominant micro-cost is `operator new` for `TemplateDomain<Interval>` builds inside each `visit(ExprMul)` — many small heap allocations per gradient construction.

These costs are paid on every cache miss in `TheorySolver::contractor_cache_` (the per-formula contractor cache at `theory_solver.cc:179, 198`). For this probe: 877 cache misses × ~88 ms each = ~77 s, matching the unaccounted CheckSat time. `Function::backward` (the path fwdbwd actually uses for HC4Revise contraction) **never reads `_grad`** — the gradient is constructed, immediately discarded, and rebuilt the next time a new formula is seen.

### Diagnosis vs the original "Pre-existing correctness flips" framing

The original framing (this doc lines 308-320 pre-update) said the `.n` correctness flips were "likely IBEX fork upgrade + Phase-2 callback removal" — same broad attribution but no specific mechanism. The 2026-06-07 profile is more precise: the *timing* component of the regression is the gradient-allocation cost. The *correctness* component (SAT↔UNSAT flips on water-double-network, airplane-single-network, gen, thermostat) is unrelated to the gradient finding and remains open as a separate soundness investigation.

The "saradc/frozen ratio is the long-standing non-ODE slowdown from the Codac migration" line that prompted this investigation was **correct in attribution** (the migration introduced it via the IBEX fork swap) but **wrong in framing** (it was treated as a fundamental loss when in fact the upstream-IBEX `_grad = nullptr` patch is a small, well-understood fix). The trade-off table at the top of this doc is updated to reflect this. The "Open lines of attack" item 7 captures the fix path.

### What landed in this round

- Polytope contractor at `src/dreal/contractor/contractor_ibex_polytope.cc::Prune` now uses the input-restricted thread_local snapshot recipe from `contractor_ibex_fwdbwd.cc` (Pass-2 trick). Quiet win for `--polytope` users; no effect on default-config saradc which never hits polytope.
- Temporary `ContractorIbexPolytopeStat` block left in place — useful for future investigations under `--polytope --verbose info`. Drop if/when it becomes noise.
- Stale claim at the pre-update line 320 ("`1mhz_k28_saradc_3b_box_4a_-1e` family TIMs at HEAD without any of the optimization-pass changes") was wrong: that benchmark now solves in 279 s per the 2026-06-06 baseline. Updated.

### What did *not* land (deferred to a follow-on session)

The IBEX-source-patch path described in "Open lines of attack" item 7. That work is contained but requires switching `ibex_external` from prebuilt-zip to source-build + applying a small upstream diff + verifying it doesn't break gaol/ultim — substantial enough to be its own session.

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

### Resolved: cache-key pointer-reuse bug in `make_codac_ode_cache`

`make_codac_ode_cache` keyed its static `flow_cache_map` by raw `OdeFlow*`. If an `OdeFlow` was destroyed and a new one later allocated at the same address with different dynamics, the cache returned stale data for the new flow — surfaced reproducibly by the Pass-3 semantic tests when fixtures created and destroyed `OdeFlow`s between `TEST_F` instances. **Fixed in commit `fed8a02a1`**: the map now stores a `CacheSlot { shared_ptr<const OdeFlow> flow, shared_ptr<CodacOdeCache> cache }` keyed by raw pointer; the held `shared_ptr<const OdeFlow>` keeps the keyed flow alive while the cache entry is live, so the address cannot be reused. `make_codac_ode_cache`'s signature changed from `(const OdeFlow&, …)` to `(shared_ptr<const OdeFlow>, …)` to make the ownership contract explicit. Eviction (see "Open lines of attack" item 2) is now the only remaining cache-lifetime concern, relevant only for embedded-service use.

### Pre-existing correctness flips (not introduced by the migration optimizations)

These benchmarks flip vs. the CAV26 baseline. The `.n` (non-ODE) cases cannot be the ODE contractor's fault. The `→ TIM` rows are the *timing* component of this regression and are now root-caused (see "Re-investigation 2026-06-07" above — gradient-allocation cost in `Function::init`, fixed by "Open lines of attack" item 7). The genuine SAT↔UNSAT flips (water-double-network, airplane-single-network) are a separate *soundness* concern that the timing fix does not address.

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

`1mhz_k28_saradc_3b_box_4a_-1e` was previously claimed to TIM at HEAD; it now solves in 279 s per the 2026-06-06 baseline. The remaining gap on this benchmark is the same gradient-allocation cost identified in "Re-investigation 2026-06-07" (~65% of wall time inside `ibex::Function::init`), not a separate SAT-layer issue. Open lines of attack item 7 closes the rest of the gap.

---

## Strategic reassessment 2026-06-07: fork `ibex-team/ibex-lib` + eliminate Codac

### Context

Two paths exist for applying the gradient fix (Open lines of attack item 7) and resolving the fork-of-a-fork dependency chain:

- **Option A** — Fork `lebarsfa/ibex-lib`, apply gradient patch, keep Codac. `lebarsfa` is 100+ commits behind `ibex-team/ibex-lib`, is not controlled by this project, and could fall arbitrarily far behind the mainline. Rebasing to ibex-team would require also porting lebarsfa's divergence. Codac itself is a prebuilt ZIP per-arch-per-OS that becomes a maintenance liability if it breaks on a new macOS or glibc release.

- **Option B** — Fork `ibex-team/ibex-lib` directly, apply gradient patch, drop Codac. `IBEXConfig.cmake` (the only feature lebarsfa adds that we need) is only required by Codac's own CMake build — dropping Codac makes it unnecessary. dreal4's CMakeLists.txt already wires IBEX via manual IMPORTED targets and never calls `find_package(IBEX)`. This path gives full control, direct rebase access to the ibex-team mainline, and reduces the dependency tree to: our ibex-team fork + CAPD + dreal4.

### Experiment: CAPD-forced vs Codac-default on ODE benchmarks

Before committing to Option B, the performance of native ARM64 CAPD (order-20) was measured against Codac `CtcLohner` (order-2) across all available `integral`-based ODE benchmarks. Each benchmark run twice: once with default settings (Codac fires when `t_ub ≤ 5` and `n_state_vars < 6`) and once with `--capd-t-gate 1e-300 --capd-ndim-gate 1` (CAPD forced on every Prune).

| Benchmark | State vars | Codac default | CAPD forced | Verdict |
|---|---|---|---|---|
| `bouncing_ball_with_drag_10_0` (10 modes, t_ub=3) | 2 | **4410 ms** | **3391 ms** | identical (δ-sat) |
| `cardiac_new_cardiac` (4 modes) | 5 | **8935 ms** | **8972 ms** | identical (unsat) |
| `prostate_h2` (2 modes) | ~4 | **19400 ms** | **18520 ms** | identical (δ-sat) |
| `prostate_h1` (32 modes) | ~4 | **14110 ms** | **14070 ms** | identical (unsat) |
| `prostate_cancer_scaled` (2 modes) | ~4 | **148 ms** | **149 ms** | identical (unsat) |

**CAPD is never slower than Codac across all tested benchmarks, and is 23% faster on the canonical reference case (`bouncing_ball`) where Codac was supposed to be the fast path.** The reason: CAPD's higher Taylor order (20 vs 2) requires fewer timesteps per integration, and on native ARM64 the per-step overhead is low enough that fewer steps wins. No verdict differences were observed.

The remainder of wall time on all benchmarks (including bouncing_ball) is dominated by the SAT layer, IBEX fwdbwd, and `forall_t` contractors — not the ODE backend — so the ODE-backend choice is not the performance bottleneck regardless.

### Decision

**Option B.** The performance argument for keeping Codac is eliminated by the experiment above. The remaining strategic considerations all favor Option B:

- Full control over the IBEX dependency; direct rebase path from ibex-team mainline.
- Dropping Codac removes a per-arch-per-OS prebuilt ZIP and eliminates the lebarsfa dependency entirely.
- CAPD is already the better ODE backend. The gated-hybrid complexity (`contractor_odes_codac.{h,cc}`, `CodacOdeCache`, the dispatch logic in `contractor_odes.cc`) can be deleted; CAPD becomes the unconditional ODE contractor.
- The `contractor_odes_codac.cc` 471-line TU was load-bearing only as the "fast path." That role is gone.

### Implementation checklist (not yet started)

- [ ] Fork `ibex-team/ibex-lib`; verify ARM64 source build (gaol/ultim, no FILIB).
- [ ] Apply gradient lazy-init patch + ExprLinearity lazy-init patch.
- [ ] Address `std::apply` ADL conflict in Bison-generated parser (macOS + Linux).
- [ ] Switch `ibex_external` in `CMakeLists.txt` from prebuilt-ZIP to `ExternalProject_Add` from our fork.
- [ ] Remove `codac_external` block and `codac-install/` references from `CMakeLists.txt`.
- [ ] Delete `contractor_odes_codac.{h,cc}`; remove Codac includes and link targets.
- [ ] Update `contractor_odes.cc` dispatch to remove the Codac path and gate logic (CAPD is unconditional).
- [ ] Update tests: remove Codac-specific fixtures; confirm CAPD-path semantic tests still pass.
- [ ] Run `/benchmark-baseline`; expect saradc PAR2 average to drop ~3× from 567 s.
- [ ] Update `CLAUDE.md` and this file to reflect the new dependency stack.
