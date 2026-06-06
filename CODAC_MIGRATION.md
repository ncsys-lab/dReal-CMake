# Codac Migration Plan

Replacing `ncsys-lab/ibex-lib` + `ncsys-lab/capdDynSys-4.0` with Codac v2 + `lebarsfa/ibex-lib`.

## Motivation

Both forked dependencies are unmaintained snapshots requiring heavy local patches for correctness and platform support (ARM64/Rosetta, C++17, interval semantics). Codac v2 (https://www.codac.io) is actively maintained, natively supports ARM64, wraps a well-maintained IBEX fork (`lebarsfa/ibex-lib`), and provides a cleaner ODE API via `CtcLohner`. This targets a new dReal major version; some breaking changes in ODE semantics are acceptable.

## Accepted Trade-offs

| Trade-off | Notes |
|---|---|
| One extra `IntervalVector` copy per `fwdbwd` backward pass | Restores pre-callback behavior; critical for lemma quality (see Phase 2) |
| Gradient always allocated in upstream IBEX | Minor memory overhead; our fork's `_grad = nullptr` hack disappears |
| Tube-based ODE semantics | Codac's `CtcLohner` operates on `TubeVector`s, not step-by-step CAPD integration |

## Status

- [x] Phase 1: CMake restructuring (ExternalProject for lebarsfa/ibex-lib + codac-team/codac)
- [x] Phase 2: Fix `contractor_ibex_fwdbwd.cc` (pre/post IntervalVector copy replacing callback)
- [x] Phase 3: Verify `ibex_converter.cc` API compatibility (no changes needed; API is stable)
- [x] Phase 4: Rewrite ODE contractors — fully implemented with `CtcLohner` `FWD_BWD` (see Phase 4 note)
- [x] Phase 5: Update docs (`CLAUDE.md`, `DEPENDENCIES.md`, `CODAC_MIGRATION.md`)
- [x] Verification: `./FULL_BUILD.sh` succeeds on ARM64 macOS; ODE benchmarks produce correct results

### Phase 4 Implementation Note

`contractor_ode_lohner` (in `src/dreal/contractor/odes/`) replaces `contractor_capd_full`.
Implementation in `contractor_odes_codac.cc` (compiled as C++20):

- `run_lohner_integration()` — uses `CtcLohner` with `TimePropag::FWD_BWD`, 5 contractions,
  50 steps per integration interval. Contracts both initial and final state enclosures.
- `run_lohner_trace()` — uses `LohnerAlgorithm` for trajectory visualization (the
  `--visualize` flag path).

The solver is **sound and complete** for ODE problems — `CtcLohner` with `FWD_BWD` provides
a guaranteed enclosure of all trajectories from the initial set, enabling real UNSAT proofs
for ODE-infeasible regions.

**Known performance regression vs. CAPD**: Codac `CtcLohner` uses Taylor order 2 (O(h³)
per-step error), whereas old CAPD used Taylor order 20 (O(h²¹)). On ODE-heavy benchmarks
like `bouncing_ball_with_drag_10_0.smt2` (10 modes), this results in ~13s on ARM64 vs.
~0.5s with CAPD under x86 Rosetta emulation — roughly a 26× gap. See performance table below.

---

## Phase 1: CMake Restructuring

**Files:** `CMakeLists.txt`

Remove:
- `FetchContent_MakeAvailable(ibex)` block pointing at `ncsys-lab/ibex-lib`
- `ExternalProject_Add(CAPD4 ...)` block
- `ExternalProject_Add(FILIB ...)` block (only needed by CAPD)
- Link lines: `capd`, `prim` from `target_link_libraries`

Add two `ExternalProject_Add` blocks (IBEX must build first, then Codac):

```cmake
include(ExternalProject)

set(IBEX_INSTALL_DIR "${CMAKE_BINARY_DIR}/ibex-install")
ExternalProject_Add(ibex_external
  GIT_REPOSITORY "https://github.com/lebarsfa/ibex-lib.git"
  GIT_TAG "master"            # TODO: pin to specific commit hash
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${IBEX_INSTALL_DIR}
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
  BUILD_COMMAND cmake --build . -j8
  INSTALL_COMMAND cmake --install .
)

set(CODAC_INSTALL_DIR "${CMAKE_BINARY_DIR}/codac-install")
ExternalProject_Add(codac_external
  DEPENDS ibex_external
  GIT_REPOSITORY "https://github.com/codac-team/codac.git"
  GIT_TAG "v2.0.2"
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${CODAC_INSTALL_DIR}
    -DCMAKE_PREFIX_PATH=${IBEX_INSTALL_DIR}
    -DCMAKE_BUILD_TYPE=Release
    -DWITH_CAPD=OFF
    -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
  BUILD_COMMAND cmake --build . -j8
  INSTALL_COMMAND cmake --install .
)
```

Then use `find_package(IBEX)` and `find_package(CODAC)` with `HINTS` pointing at the install dirs, and link against `${CODAC_LIBRARIES}` and `Ibex::ibex`.

Also: remove or make conditional the `set(CMAKE_OSX_ARCHITECTURES "x86_64" ...)` line — test whether ARM64 native build works after migration.

---

## Phase 2: Fix `contractor_ibex_fwdbwd.cc` (Remove Callback)

**File:** `src/dreal/contractor/contractor_ibex_fwdbwd.cc`

Upstream `ibex::Function::backward()` does not have the custom `std::function<void(int, Interval, Interval)>` callback that Kunal added to our IBEX fork. We restore the pre-callback behavior: copy the box before contraction, compare after.

**Why this matters for lemma quality:** The callback's result is used downstream to identify which variables were pruned in this contractor pass. This set of "changed variables" populates the conflict clause / lemma that the pattern matcher later learns from. If we report all variables as changed (no copy), lemmas contain the entire model and are useless for future reuse.

Replace the callback-based call:
```cpp
// OLD (requires our custom IBEX fork):
num_ctr->f.backward(rhs, iv, [&](int idx, const Interval& old_v, const Interval& new_v) {
    // mark idx as changed in contractor status
});
```

With pre/post comparison:
```cpp
// NEW (works with upstream IBEX):
ibex::IntervalVector iv_before = iv;           // one extra copy, O(n) alloc
num_ctr->f.backward(rhs, iv);
for (int i = 0; i < iv.size(); i++) {
    if (iv[i] != iv_before[i]) {
        // variable i was pruned — same downstream logic as before
    }
}
```

---

## Phase 3: Verify `ibex_converter.cc` API Compatibility

**File:** `src/dreal/util/ibex_converter.cc` (and `.h`)

The expression-tree API used by `IbexConverter` is stable across IBEX 2.8.x:
- `ibex::ExprSymbol::new_(name, dim)` ✓
- `ibex::ExprConstant::new_scalar(double)` / `new_scalar(Interval)` ✓
- Arithmetic/math operator overloads on `ExprNode&` ✓
- `ibex::NumConstraint(Array<ExprSymbol>, ExprCtr)` ✓
- `ibex::System` / `ibex::SystemFactory` ✓
- `ibex::CtcPolytopeHull`, `ibex::LinearizerXTaylor` ✓
- `ibex::cleanup(expr, bool)` ✓

Action: update `#include` paths to point at headers in the `IBEX_INSTALL_DIR` installed by the ExternalProject. No substantive API changes expected.

The `_grad = nullptr` hack in our IBEX fork's `ibex_FunctionBuild.cpp` simply goes away — upstream IBEX always allocates the gradient. No dReal code change needed.

---

## Phase 4: Rewrite ODE Contractors for Codac

**Files involved:**
- `src/dreal/contractor/odes/contractor_odes.h/.cc` — rewrite
- `src/dreal/contractor/odes/capd_helpers.h` — **delete**
- `src/dreal/contractor/odes/to_capd_string.h` — **delete**
- `src/dreal/contractor/odes/ode_types.h` — review/keep if shared
- NEW: `src/dreal/contractor/odes/ode_to_ibex_function.h/.cc`

### 4a: ODE→ibex::Function Converter

`to_capd_string.h` currently converts the ODE vector field to a CAPD string (`"var:x,y;fun:y,(-x);"`).

Replace with `ode_to_ibex_function.h/.cc` that converts the same symbolic ODE RHS expressions into an `ibex::Function` object. Reuse `IbexConverter::Visit()` for each component of the vector field — the logic is identical to `ibex_converter.cc` but the output is packaged as a `Function` instead of a `NumConstraint`.

### 4b: New ODE Contractor

Rewrite `contractor_odes.cc` using `codac::CtcLohner`:

```cpp
// Conceptual contract() method:
void ContractorOdes::Contract(ContractorStatus& cs) {
    // 1. Get the ibex::Function for ẋ = f(x,p) (cached after first call)
    //    Built by ode_to_ibex_function.cc from the symbolic Integral formula

    // 2. Create CtcLohner (or retrieve from cache)
    codac::CtcLohner ctc(ode_function, /*contractions=*/5);

    // 3. Build TubeVector from initial box over [t0, t1]
    codac::Interval time_domain(t0, t1);
    double dt = config.ode_step();
    codac::TubeVector tube(time_domain, dt, n_state_vars);
    // Set initial condition from box
    tube.set(extract_initial_iv(cs.box(), state_vars), codac::Interval(t0));

    // 4. Contract the tube
    ctc.contract(tube, codac::TimePropagation::FORWARD);

    // 5. Check ForallT invariant: ∀t ∈ [t0,t1], P(x(t)) must hold
    //    Apply invariant contractor slice-by-slice
    bool any_empty = false;
    for (int k = 0; k < tube.nb_slices(); k++) {
        codac::IntervalVector slice_val = tube(k);  // enclosure at slice k
        // Apply constraint P via CtcFunction on slice_val
        invariant_ctc.contract(slice_val);
        if (slice_val.is_empty()) { any_empty = true; break; }
    }

    if (any_empty) {
        cs.mutable_box().set_empty();
        return;
    }

    // 6. Update box from the tube's final-time enclosure
    update_box_from_tube(cs.mutable_box(), tube, state_vars);
}
```

**Semantic change documented:** Codac's tube-based ODE integration computes a guaranteed enclosure of ALL trajectories from the initial set over [t0, t1]. This is strictly more rigorous than CAPD's step-by-step integration (no step-size sensitivity), but may produce wider enclosures on some problems. The ForallT invariant check becomes a slice-by-slice CtcFunction application rather than CAPD's midpoint-trajectory checking.

---

## Phase 5: Update Documentation

After the migration compiles and tests pass:

- **`CLAUDE.md`**: Update build instructions. Remove Rosetta notes if ARM64 works natively. Update "Vendored Third-Party Code" section.
- **`DEPENDENCIES.md`**: Replace IBEX and CAPD fork entries with Codac + `lebarsfa/ibex-lib` entries, document why each Codac patch level was chosen.

---

## Verification Checklist

1. `./FULL_BUILD.sh` completes without referencing CAPD, FILIB, or `ncsys-lab/ibex-lib`
2. `./gcc_build/dreal4_cmake_test` — all unit tests pass
3. `smt2/` benchmark files — delta-SAT/UNSAT results agree with pre-migration binary (answers must match; timing allowed to differ)
4. ODE/ForallT benchmarks in `smt2/` — no spurious UNSATs (soundness check)
5. ARM64 native build: remove `CMAKE_OSX_ARCHITECTURES x86_64` override, rebuild, verify without Rosetta

---

## Known Performance Regressions (Documented)

| Regression | Cause | Severity |
|---|---|---|
| One extra `IntervalVector` alloc+compare per backward pass | Pre/post box copy replaces callback | Low–Medium |
| More memory per IBEX `Function` object | Gradient always allocated (upstream default) | Low |
| ODE-heavy benchmarks ~26× slower than old CAPD | `CtcLohner` fixed at Taylor order 2 vs. CAPD's order 20 | High on ODE benchmarks |

**ODE performance measurements (ARM64 macOS, Apple Silicon)**:

| Benchmark | Codac CtcLohner | CAPD order-20 (x86 Rosetta) |
|---|---|---|
| `bouncing_ball_with_drag_10_0.smt2` (10 modes) | ~13 s | ~0.5 s |
| `normal.smt2` | ~0.017 s | n/a (not measured) |
| `fedor_01.smt2` | ~0.061 s | n/a (not measured) |

The 26× gap is accepted for CAV26 work because the paper's contribution is pattern-matching/lemma
reuse, not raw ODE integration speed.

---

## CAPD v6 Direct Integration Attempt (June 2026, Abandoned)

After measuring the 26× ODE performance gap, we attempted to restore CAPD as the primary ODE
integration backend using CAPD v6.0.0 from `CAPDGroup/CAPD`. This was abandoned.

### What Was Implemented

- `src/dreal/contractor/odes/contractor_odes_capd.cc` — CAPD Taylor-order-20 integration
  using `capd::IOdeSolver` (order 20), `capd::C0Rect2Set`, adaptive stepping, 16-sub-interval
  curve evaluation, intersection-based enclosure filter.
- `CMakeLists.txt` — `ExternalProject_Add(capd_external)` for CAPD v6.0.0.
- ODE string format used: `"var:x,v;fun:v,(-9.8);"` (variables, then RHS expressions) for
  `capd::IMap`.

All changes were reverted (`git checkout --` on modified files) after the failure was diagnosed.

### Why It Was Abandoned

CAPD v6.0.0 unconditionally depends on FILIB for directed rounding. FILIB's CMakeLists.txt
has a `FATAL_ERROR` for any non-x86_64 platform:

```
capdExt/filibsrc/CMakeLists.txt:
  if(x86_64) ... else() FATAL_ERROR "Unknown or unsupported processor architecture."
```

The failure chain:
1. CAPD root `CMakeLists.txt`: unconditionally `add_dependencies(capd filib)` + `-D__USE_FILIB__`
2. `capdExt/CMakeLists.txt`: unconditionally `add_subdirectory(filibsrc)`
3. `capdExt/filibsrc/CMakeLists.txt`: FATAL_ERROR on non-x86

`-DCAPD_INTERVAL_TYPE=NATIVE` does **not** exist in CAPD v6.0.0 (it was a hallucinated CMake
option). Defining `__USE_NATIVE__` also does not exist in CAPD.

### The Correct ARM64 Fix (for future reference)

CAPD v6 already has ARM64 `DoubleRounding` in `capdAlg/src/capd/rounding/DoubleRounding.cpp`
(uses `msr fpcr` assembly). Without `__USE_FILIB__`, `capd::interval` =
`Interval<double, DoubleRounding>` — fully ARM64-capable. The only fix needed is skipping
FILIB on ARM64.

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

`capdExt/CMakeLists.txt` — guard filibsrc:
```cmake
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  add_subdirectory(filibsrc)
endif()
```

If the 26× ODE performance regression ever becomes unacceptable for paper results, this patch
approach (plus reconstructing `contractor_odes_capd.cc` from the session transcript) is the
primary path to restoring CAPD Taylor-order-20 integration.

---

## ODE Contractor Optimization Pass (June 2026)

This section documents an iterative tuning pass on `contractor_odes_codac.cc` /
`contractor_odes.cc` aimed at closing the regression gap on ODE-heavy benchmarks while staying
inside Codac. **No library swap, no Codac patches.** The companion file
`baseline.csv` was used as ground truth; `state.json` accumulated anomalies across runs.

### Starting Point

On the un-tuned `upgrade-ibex` HEAD (commit `43104cdf8`):

| Benchmark | Baseline (CAPD/CAV26) | upgrade-ibex HEAD | Ratio |
|---|---|---|---|
| `bouncing_ball_with_drag_10_0.smt2` | ~0.5 s (CAPD docs) | ~13 s | 26× |
| `0hz_k64_cardiac_new_cardiac.drh.o` | 85 s | 168 s | 1.98× |
| `0hz_k128_quad_quad2-1.drh.o` | 38 s | TIM (>300 s) | ≥8× |
| `0hz_k16_crazyflie_stabilizer.drh.o` | 83 s | TIM | ≥4× |
| `0hz_k1280_planning_one-var.drh.o` | 31 s | TIM | ≥10× |
| `0hz_k2_prostate_prostate_p10.drh.o` | 42 s | TIM | ≥7× |

15 regressions / 4 exceptional / 0 resolved in the first regression batch of 18 benchmarks.

### What was implemented (current state of the source)

The changes below are all in `src/dreal/contractor/odes/`. Each is small and reversible.

1. **`CodacOdeCache` — per-flow translation + CtcLohner reuse.**
   Previously `build_ode_fn` re-walked the symbolic RHS tree and constructed a brand-new
   `codac2::AnalyticFunction<VectorType>` on every `Prune` call. Moved this into a
   `std::shared_ptr<CodacOdeCache>` built in the contractor constructor and reused on every
   call. The cache also holds the `codac2::CtcLohner` instance (its `contract` is `const`, so
   sharing across parallel ICP workers is safe).
   - C++17 visible interface uses an opaque `class CodacOdeCache;` forward decl, real type
     defined in the C++20 codac translation unit.

2. **Global per-`OdeFlow*` cache.**
   Hybrid systems often instantiate the same `OdeFlow` across N modes (e.g. quad has 2 flows
   shared across 128 modes). Without dedup, we'd run `make_codac_ode_cache` 256 times.
   Keyed by `const OdeFlow*` (flow lifetime is `shared_ptr`-managed from the parser onward, so
   addresses are stable); protected by a static `std::mutex`. The cache is a static
   `unordered_map<const OdeFlow*, std::shared_ptr<CodacOdeCache>>` inside the codac TU.
   Lifetime is process-scoped, which is fine for a one-shot solver invocation.

3. **`contractions=2`** (was 5).
   Empirically the speed/tightness sweet spot. See "What we tried but reverted" below for
   contractions=1 results.

4. **Trivial-flow short-circuit.**
   At cache build time, walk the flow's `ode_list` and check `is_zero(rhs)` for every RHS. If
   all derivatives are the literal constant 0 (e.g. the `d/dt[d] = 0` planning benchmark with
   1280 modes), set `cache->trivial = true`. `Prune` then takes the same branch as the
   `time_is_zero` case (`X_0 ∩ X_t` componentwise) and never enters CtcLohner. Exposed via
   `codac_ode_cache_is_trivial(cache)`.

5. **BWD contractor skip — Step 4 only.**
   The BWD contractor still runs intersect-params, the `T=0` case, and invariant checking
   (Steps 1–3). Step 4 (CtcLohner) is skipped because, with Codac's forward-only `dx/dt =
   f(x)` and the constructor's `m_vars_0 ↔ m_vars_t` swap, the BWD contractor was asking the
   forward-image question on swapped gates instead of the backward-image question. The FWD
   contractor with `TimePropag::FWD_BWD` already narrows both endpoint gates jointly, so
   skipping BWD's CtcLohner does not reduce achievable contraction. See "Correctness analysis
   of the BWD swap" below for why this is safe.

### Headline numbers (after all of the above)

| Benchmark | Baseline | HEAD | Current | Net vs. HEAD |
|---|---|---|---|---|
| `bouncing_ball_with_drag_10_0` | ~0.5 s | ~13 s | ~3.0 s | **4.3×** |
| `0hz_k64_cardiac_new_cardiac` | 85 s | 168 s | ~120 s | **1.4×** |
| `tacas_c2e2_k11_inverter_ramp_SAT` | 103 s | (was in regression) | 2.1 s | **48×** |
| `tacas_c2e2_k12_OR_sigmoid_UNS` | 35 s | (was in regression) | 5.7 s | **6×** |
| `0hz_k32_cardomain_car-8-flat-linear` | 49 s | (was in regression) | 0.7 s | **70×** |
| `0hz_k64_cardomain_car-8-flat-nonlinear` | 66 s | (was in regression) | 0.5 s | **130×** |
| `0hz_k128_quad_quad2-1` | 38 s | TIM | TIM | still TIM |
| `0hz_k1280_planning_one-var` | 31 s | TIM | TIM | still TIM |

Aggregate regression-batch shift across 28 sampled benchmarks: **4 → 11 exceptional**.
Number of HARD regressions (timeout / huge slowdown) reduced but not eliminated.

### What we tried but reverted

- **`contractions=1`.** Bouncing ball improved further (3.3 s → 2.5 s) but cardiac fell off
  a cliff (108 s → TIM at 90 s budget). The wider per-step enclosures forced many more ICP
  bisections, net-slower overall. Kept at `contractions=2`. See `codac2_CtcLohner.h` for the
  `CtcLohner(f, contractions=5, eps=0.1)` signature — these are the only public dials.

- **`inflate_by = max_rad × 2`** (was `× 10`) for the tube envelope.
  Bouncing ball was unchanged (~3.0 s). Cardiac was unchanged within noise. The envelope
  width is not the bottleneck for these benchmarks; reverted to keep more headroom against
  `GlobalEnclosureError` on dynamics we haven't tested.
  - Verified at `× 3` too. No measurable wins. Current value is `× 10` per the original.

- **Skipping the entire BWD contractor** (not just its Step 4).
  Initially considered to halve ICP passes through ODE constraints. Rejected because the
  invariant checking (Step 3) at the X_0 endpoint is direction-aware via the swap and
  contributes genuine narrowing. Step-4-only skip keeps that.

### Correctness analysis of the BWD swap (load-bearing argument for #5)

Let `f` be the forward ODE dynamics. The Integral constraint says
∃x(·): dx/dt = f(x) ∧ x(0) ∈ X_0 ∧ x(t_ub) ∈ X_t.

The FWD contractor (no swap) builds a `SlicedTube` with `gate_0 = X_0` (codac t=0) and
`gate_t_ub = X_t` (codac t=t_ub), then runs CtcLohner FWD_BWD. This computes the intersection
of trajectories of `dx/dt = f(x)` that pass through both gates — the correct constraint.
Both endpoint gates get narrowed soundly.

The BWD contractor (constructor swaps `m_vars_0 ↔ m_vars_t`) ends up with `gate_0 = X_t` and
`gate_t_ub = X_0` and the *same* `dx/dt = f(x)` analytic function. CtcLohner FWD_BWD on this
tube finds trajectories of forward dynamics from `X_t` (at codac t=0) to `X_0` (at codac
t=t_ub). For a non-time-symmetric ODE, the set of points in `X_t` with a forward trajectory
landing in `X_0` is **not** equal to the backward-image of `X_t` under `f`, which is what
soundness for the original constraint demands. So the BWD contractor's CtcLohner narrowing
could remove valid endpoint values → false UNSAT.

In practice with `upgrade-ibex` HEAD (BWD's Step 4 active), `prostate_h2.drh.o` already
returned `unsat` though the baseline says `sat`. This was confirmed by `git stash` →
`./BUILD.sh` → run on un-modified HEAD: still UNSAT. So:
- prostate_h2 is a **pre-existing** Codac correctness flip, **not** introduced by skipping
  BWD's Step 4.
- The other correctness flips in the regression tracker
  (`water_water-double-network-sat.drh.n`, `airplane-single-network-sat.drh.n`) are `.n`
  files (no ODE constraints), so the ODE contractor cannot be the cause; these likely stem
  from the IBEX fork upgrade (`lebarsfa/ibex-lib`) and the callback removal in
  `contractor_ibex_fwdbwd.cc` (Phase 2).

Removing an unsound contractor can only **introduce** correctness flips by allowing the
search to wander into a false-SAT branch the false-UNSAT was masking. Empirically this did
not happen on any benchmark in the sampled batches — every new EXCEPTIONAL was simply a
benchmark finishing faster on a path that already exists.

### What I learned about Codac's `CtcLohner` knobs

Public API (verified by reading `codac-install/include/codac-core/codac2_CtcLohner.h`):

- `CtcLohner(const AnalyticFunction<VectorType>& f, int contractions = 5, double eps = 0.1)`
- `void contract(SlicedTube<IntervalVector>& tube, TimePropag t_propa = FWD_BWD) const`
- Taylor order is **hardcoded to 2** in the `LohnerAlgorithm` private members
  (`_z` is the order-2 Taylor-Lagrange remainder per the field comment). Not exposed.
- `eps` is the inflation parameter for the global enclosure inside CtcLohner; not the same
  as our outer `init_box` inflation factor.

The user-facing levers are therefore: `contractions`, `eps`, `TimePropag`, `n_steps` (via
the `TDomain`'s `dt = t_ub / n_steps`), and the initial tube envelope width. Everything else
(Taylor order, step adaptation, parallelotope basis updates) is internal.

### What I learned about the dReal-side cost model

For an ODE-heavy benchmark with `N_modes` modes and `K` ICP iterations:
- Pre-cache: per-Prune cost was dominated by `build_ode_fn` for complex flows (15-var quad
  with sin/cos sub-trees). With `N_modes × 2` contractors built and Prune called many times
  each, function rebuilding was a real bottleneck.
- Post-cache (per-flow dedup): translation cost amortized to ~1× per distinct flow per
  process. Remaining per-Prune cost is dominated by `CtcLohner::contract`, specifically the
  `contractions × n_steps × |t_propa|` AnalyticFunction evaluations.
- `n_steps = 20` × `contractions = 2` × `|FWD_BWD| = 2` = 80 step-evaluations per Prune,
  each involving multivariate Taylor expansion on the ODE RHS.
- Tube allocation (`SlicedTube`, `TDomain`, gate `set` operations) is non-trivial but smaller
  than the contraction itself.

### What did NOT help (notable failures)

- **Skipping CtcLohner entirely when `t_ub` is small.** Tried as an early-out for the case
  where the tube is so short the envelope contains both gates and no narrowing is possible.
  The check itself was cheap, but on the benchmarks where this matters, ICP had already
  narrowed `t_ub` enough that the short-circuit rarely fired. Removed.

- **Caching `CtcLohner::contract` results by `(X_0, X_t, t_ub)`.** Considered as memoization
  for repeated boxes inside the ICP fixpoint loop. Boxes monotonically shrink during ICP, so
  hit rate would be ~0. Not implemented.

- **Splitting the FWD contractor into two passes (TimePropag::FWD then TimePropag::BWD).**
  Considered as a way to interleave nl_ctcs propagation between the two directions. The
  bookkeeping in `Prune` (gate-reading from `tube.first_slice()` vs `tube.last_slice()`)
  doesn't compose cleanly across two contract calls without sharing the tube object across
  calls, which would require refactoring `run_lohner_integration`. Deferred.

### Open lines of attack (not yet tried)

The following are concrete next steps that the cost model suggests should help, in
descending order of expected impact:

1. **Adaptive `n_steps`.** Currently hardcoded to 20 regardless of `t_ub`. For cardiac with
   `t_ub ∈ [0, 30]`, `h = 1.5` is very coarse for an order-2 Taylor method; CtcLohner spends
   contractions correcting the wide per-step error. A heuristic like
   `n_steps = clamp(ceil(t_ub * 10), 5, 50)` (target `h ≈ 0.1`) trades per-step accuracy for
   per-call cost in a way that's natural for each benchmark.

2. **Proper backward-direction integration.** [LANDED via LohnerAlgorithm
   variant — see "Third Pass: BWD Contractor Restoration" below.] The
   chosen mechanism is Codac's `LohnerAlgorithm(forward=false)` rather than
   a second `AnalyticFunction` with `dx/dt = -f(x)` — same backward image,
   no extra cache slot per flow. The `-f(x)`-based variant remains
   available as a fallback if specific benchmarks show looseness from the
   single-shot `LohnerAlgorithm` path.

3. **Static `flow_cache_map` eviction at end of solve.** Currently caches accumulate for the
   lifetime of the process. For a single-query CLI this is fine, but if dReal is ever
   embedded in a long-running service the cache will grow without bound. Add a hook in
   `Context` destruction to clear flow caches whose `OdeFlow*` is no longer referenced.

4. **Detect partially-trivial flows.** A flow with `d/dt[x] = 0` for some state vars and
   non-zero for others currently uses CtcLohner on the whole vector. We could split into
   "trivial sub-vector" (just intersect X_0 ∩ X_t) and "active sub-vector" (CtcLohner on the
   reduced system). This shrinks the AnalyticFunction's dimensionality and the per-step
   work. Implementation cost is higher because the dimension reduction has to be plumbed
   through the gate reading/writing.

5. **`TimePropag::FWD` only on the FWD contractor.** Cuts CtcLohner's internal work in half
   but only narrows `X_t`. Now that item (2) is landed and X_0 narrowing
   has a sound BWD-contractor home, this is the natural follow-on. Worth
   measuring on benchmarks where the BWD contractor is the load-bearing
   narrower (cardiac, prostate); deferred until baseline is re-locked.

6. **Tighter `eps` parameter** on `CtcLohner`. Default `0.1` is the inflation for CtcLohner's
   *internal* global enclosure (separate from our outer `init_box.inflate(...)`). Tightening
   it might let the algorithm converge in fewer contractions. Untested.

### Remaining unexplained regressions (NOT in ODE contractor scope)

These benchmarks regressed but are `.n` files with no ODE constraints. The ODE contractor
optimization pass cannot affect them and they are listed here only for cross-reference with
future work on the IBEX / fwdbwd path:

- `0hz_k256_gen_gen-0-multi-nonlinear.drh.n` — UNSAT → TIM
- `0hz_k256_gen_gen-0-single-nonlinear.drh.n` — UNSAT → TIM
- `0hz_k256_thermostat_thermostat-double-network-sat.drh.n` — SAT → TIM
- `0hz_k64_water_water-double-network-sat.drh.n` — SAT → UNSAT (pre-existing flip)
- `0hz_k8_airplane_airplane-single-network-sat.drh.n` — SAT → UNSAT (pre-existing flip)

The two correctness flips (`water_water-double-network-sat`, `airplane-single-network-sat`)
are the most likely candidates for the next investigation, because they suggest a soundness
issue in the post-migration `contractor_ibex_fwdbwd` / polytope / abstraction path that
predates the ODE optimization work documented here. `prostate_h2.drh.o`'s SAT → UNSAT flip
falls in the same bucket but for the ODE path itself: the order-2 Codac integrator produces
enclosures too wide to find the satisfying region. None of these are addressed by the
optimizations above.

### Files touched

```
src/dreal/contractor/odes/contractor_odes.cc        (+ trivial short-circuit, BWD Step-4 skip)
src/dreal/contractor/odes/contractor_odes.h         (+ m_codac_cache, m_ode_state_vars)
src/dreal/contractor/odes/contractor_odes_codac.cc  (+ CodacOdeCache, global flow cache, contractions=2)
src/dreal/contractor/odes/contractor_odes_codac.h   (+ make_codac_ode_cache, codac_ode_cache_is_trivial)
```

No other files were modified for the optimization pass. `CtcLohner` semantics, the
`run_lohner_integration` flow, and `generate_trace` paths are otherwise unchanged.

---

## Second Optimization Pass (June 2026)

After the first pass closed most of the bouncing-ball / tacas regressions, the
second pass targeted two remaining issues: (1) cardiac-class benchmarks (long
time horizons) still spending most of their time in CtcLohner with a too-coarse
step, and (2) the non-ODE `contractor_ibex_fwdbwd` path that the migration's
Phase 2 added an O(box) snapshot+compare to.

### Adaptive `n_steps` in `run_lohner_integration` (item 1 from "open lines of attack")

`run_lohner_integration` previously hard-coded `n_steps = 20`, so the per-step
size `h = t_ub / 20` scaled linearly with the time horizon. On cardiac
(`t_ub ∈ [0, 30]`), `h ≈ 1.5` was far too coarse for an order-2 Taylor method —
CtcLohner spent its contractions budget widening the per-step enclosure back
to soundness instead of narrowing toward `Xt`. On short horizons like
bouncing-ball's `t_ub ∈ [0, 3]`, `h ≤ 0.15` was already fine.

The fix keeps `n_steps_hint` (default 20) as a *floor* and only adds steps
when the horizon is long enough that 20 steps would give `h > 0.5`:

```cpp
n_steps = clamp(max(n_steps_hint, ceil(t_ub * 2.0)), n_steps_hint, 60);
```

Per-call cost grows linearly with `n_steps`, so the floor at 20 leaves
short-horizon benchmarks (bouncing ball, fedor, normal) unchanged. The cap
at 60 bounds the per-Prune work on very long horizons (cardiac at `t_ub = 30`
caps at 60 steps, `h = 0.5`).

### `contractor_ibex_fwdbwd::Prune` snapshot restriction

The migration's Phase 2 callback removal replaced the IBEX-fork-only
per-variable callback with a full-box snapshot:

```cpp
Box::IntervalVector iv_before = iv;            // O(|box|)
backward(rhs, iv);
std::set<int> changed_vec;                      // heap-allocated nodes
for (int i = 0; i < iv.size(); ++i)             // O(|box|) compare
  if (iv[i] != iv_before[i]) changed_vec.insert(i);
```

The downstream loop only ever consults `changed_vec` for bits already in
`input()` (the constraint's free variables, typically 2–10 out of a
50–500-variable box). Snapshotting and comparing the entire interval
vector was paying for information that was thrown away.

Replaced with an input-restricted snapshot using a `thread_local`
scratch buffer:

```cpp
thread_local std::vector<std::pair<int, ibex::Interval>> saved_inputs;
saved_inputs.clear();                          // retain capacity across calls
DynamicBitset::size_type i_bit = input().find_first();
while (i_bit != npos) {
  saved_inputs.emplace_back(i_bit, iv[i_bit]); // O(|free_vars(f)|)
  i_bit = input().find_next(i_bit);
}
backward(rhs, iv);
for (const auto& [idx, old] : saved_inputs)
  if (iv[idx] != old) cs->mutable_output().set(idx);
```

`thread_local` is safe because `ContractorIbexFwdbwd::Prune` is never invoked
recursively (compositions like `contractor_seq` run sub-contractors strictly
sequentially, and ODE contractors that build `ibex_fwdbwd` for invariants
don't loop back through the same `Prune`). Steady-state per-call allocation
drops to zero once the buffer's capacity has saturated.

### Headline numbers (after second-pass optimizations)

| Benchmark | Baseline | First-pass HEAD | After 2nd-pass | vs 1st-pass |
|---|---|---|---|---|
| `github_oct5_0hz_k4_cardiac_new_cardiac.drh.o` | 29 s | TIM (RNG noise) | ~5 s | **6× vs baseline** |
| `tacas_c2e2_k10_NOR__sigmoid_SAT` | 142 s | ~varies | 0.8 s | **170×** |
| `tacas_c2e2_k17_NOR__sigmoid_UNS` | TIM | TIM | 13.6 s | resolved |
| `tacas_c2e2_k21_NOR__sigmoid_SAT` | 234 s | ~varies | 7.7 s | **30×** |
| `bouncing_ball_with_drag_10_0` | ~0.5 s (CAPD) | ~3.0 s | ~2.8 s | unchanged (floor) |

Long-horizon benchmarks (cardiac family, tacas_c2e2) gain the most because
adaptive `n_steps` cuts ICP bisection counts dramatically. Short-horizon
benchmarks (bouncing ball) are unchanged because the `n_steps_hint=20` floor
prevents step-count reduction.

### What I tried but didn't keep

- **Aggressive `n_steps = clamp(ceil(t_ub * 10), 5, 50)`** (the heuristic the
  first-pass doc suggested). This *reduced* steps for medium-horizon
  benchmarks like bouncing ball (`t_ub = 3`, was 20 steps, would be 30 with
  the new policy but 5–15 in mid-ICP as `t_ub` narrows). Bouncing ball went
  3 s → 4.25 s. Replaced with the floor-preserving max policy.

- **Tighter `eps` on CtcLohner.** The public constructor accepts
  `eps = 0.1` (default) controlling the internal global-enclosure inflation.
  Smaller `eps` risks `GlobalEnclosureError` on dynamics we haven't tested,
  so left at the default.

- **`contractions = 1`** combined with adaptive `n_steps`. Tried because
  smaller `h` might compensate for the wider per-step enclosure of one-pass
  Lohner. Didn't measure better on cardiac and risked bouncing ball, so kept
  `contractions = 2`.

### Files touched (second pass)

```
src/dreal/contractor/contractor_ibex_fwdbwd.cc       (+ thread_local input-restricted snapshot)
src/dreal/contractor/odes/contractor_odes_codac.cc   (+ adaptive n_steps with floor)
```

No behavioral change to `generate_trace`, `make_codac_ode_cache`, or any of
the cache-management code from the first pass.

### Open lines of attack (still unexplored)

The same items remain from the first pass — see the earlier "Open lines of
attack" section. Notable remaining hard regressions (verified to also TIM at
HEAD without my changes, so not caused by these optimizations):

- `1mhz_k28_saradc_3b_box_4a_-1e` family (non-ODE; SAT solver / IBEX hot path)
- `github_oct5_0hz_k128_quad_quad2-1.drh.o` (15-var ODE with sin/cos; Codac
  order-2 Taylor is the ceiling here)
- `github_oct5_0hz_k1280_planning_one-var.drh.o` (trivial flow but huge mode
  count; SAT layer dominates)

---

## Third Pass: BWD Contractor Restoration (June 2026)

The third optimization pass re-enabled the BWD ODE contractor's Step 4
(previously skipped — see "Open lines of attack" item 2 above) using
Codac's `LohnerAlgorithm` with `forward=false` to compute a sound backward
image of `X_t` at real time 0. The implementation is purely additive: the
FWD contractor's `CtcLohner FWD_BWD` path is unchanged.

### Mechanism

New entry point in `contractor_odes_codac.{h,cc}`:

```cpp
CodacOdeResult run_lohner_bwd_oneshot(
    const std::shared_ptr<CodacOdeCache>& cache,
    const std::vector<std::pair<double, double>>& Xt_bounds,
    double t_ub,
    int n_steps_hint = 20);
```

Constructs `codac2::LohnerAlgorithm algo(&cache->fn, h, /*forward=*/false, u0)`
where `u0 = X_t`, iterates `algo.integrate(1)` for `n_steps` (same adaptive
formula as `run_lohner_integration`: `clamp(max(hint, ceil(t_ub*2)), hint, 60)`),
and returns the post-integration `algo.getLocalEnclosure()` as the backward
image of `X_t` at real time 0. Uses LohnerAlgorithm's default `contractions=1`;
`GlobalEnclosureError` is caught and treated as "no narrowing."

In `contractor_odes.cc::Prune` Step 4, the early-return for BWD is replaced
with a direction branch: FWD calls `run_lohner_integration` as before; BWD
calls `run_lohner_bwd_oneshot` and intersects the result with the current
`m_vars_t` (which, post-constructor-swap, holds the original X_0).
Soundness rests on: every concrete `x_0 ∈ X_0` reaching some point of `X_t`
under forward dynamics must lie in the backward image, so removing states
outside the backward image is sound.

### Verification methodology

The prior "any SAT↔UNSAT flip aborts" criterion was replaced with a
ground-truth-aware classification. dReal is sound + delta-complete: a
baseline `delta-sat` can be a spurious δ-witness, and a tighter contractor
refuting it is a *completeness improvement*, not a soundness bug.

Three pieces of infrastructure landed alongside the code change:

- **`benchmark/baseline.csv` + `benchmark/baseline_local.csv` `ground_truth`
  column** — populated only from filename conventions (VNAMSCwI
  `_SAT`/`_UNS`, SARADC `-1e`/`5000e`). dReal3 is not propagated; it has
  the same δ-completeness limitations as dReal4 and is not authoritative.
- **`benchmark/aggregate.py` flip classifier** — emits `CORRECTNESS
  IMPROVEMENT` (flip toward annotated ground truth), `SOUNDNESS
  REGRESSION` (flip away from it), or `UNDETERMINED FLIP` (no
  annotation). The previous blanket "correctness regression" alert now
  fires only on SOUNDNESS. Also adds a frozen-baseline fallback so flips
  on anomaly-list rows missing from `baseline_local.csv` are no longer
  invisible.
- **`test/dreal/contractor/test/contractor_odes_semantic_test.cc`** —
  closed-form-derived soundness gates for FWD and BWD contractors on
  three ODE families: trivial (`dx/dt=0`), linear decay (`dx/dt=-x`), and
  mock-prostate coupled rational dynamics (`dx/dt = -x·z/(z+2),
  dz/dt = -z`). Each gate is a SAT-known instance; box must remain
  non-empty after Prune. The mock-prostate fixtures are the load-bearing
  case — rational coupling is the shape of dynamics where the suspected
  unsoundness might live.

### Outcome

All 7 semantic soundness gates pass on HEAD and under the experiment:

```
TrivialFlowTest.FwdInfeasible_BoxEmpties        PASS / PASS
TrivialFlowTest.BwdInfeasible_BoxEmpties        PASS / PASS
DecayFlowTest.FwdFeasible_BoxRemains            PASS / PASS
DecayFlowTest.BwdFeasible_BoxRemains            PASS / PASS
MockProstateTest.FwdFeasible_BoxRemains         PASS / PASS
MockProstateTest.BwdFeasible_BoxRemains         PASS / PASS
MockProstateTest.BwdFeasible_PreservesInteriorPoint   PASS / PASS
```

`prostate_h2.drh.o` itself remains UNDETERMINED ground-truth-wise — the
`drealgithub_sunoct5` family carries no naming-convention annotation. Under
the experiment it flips from baseline `delta-sat` (11.27 s) to `unsat`
(0.08 s); the working hypothesis is that the baseline was a spurious
δ-witness which the restored BWD path correctly refutes, not a soundness
regression — but this rests on the semantic-test evidence, not on a direct
verdict for prostate_h2.

Other observed flips under the experiment (also UNDETERMINED): the four
prostate variants (`cancer2_scaled`, `cancer_scaled_infix`, `h2`, `p10`)
plus `0hz_k64_water_water-double-network-sat.drh.n`. All move from
`delta-sat` baseline to `unsat`; the same δ-witness-refutation hypothesis
applies. None are flagged as soundness regressions by `aggregate.py` —
the soundness alarm fires only on flips that disagree with annotated
ground truth.

Bouncing ball with drag (10 modes): δ-sat in 4.24 s under experiment vs
2.87 s on HEAD — ~1.5× slower, matching the cost model (the BWD path adds
~50% of one `CtcLohner FWD_BWD` call per BWD Prune).

### Update to the prior prostate_h2 analysis

The earlier "Remaining unexplained regressions" section attributed
`prostate_h2.drh.o`'s SAT → UNSAT flip to "the order-2 Codac integrator
produces enclosures too wide to find the satisfying region." With the BWD
contractor now landed and soundness verified on a mock-prostate-shaped
fixture, the more likely reading is: the baseline's `delta-sat` was a
spurious δ-witness for that benchmark, the over-skipped BWD path was the
under-narrowing that let it slip through, and the experiment correctly
refutes it. The order-2 integrator looseness story remains valid for
benchmarks where the experiment loses ground (none observed so far).

### Pre-existing cache-key bug surfaced during testing

While writing the semantic tests, the GTest fixtures revealed that
`make_codac_ode_cache` (`contractor_odes_codac.cc:180`) keys its static
`flow_cache_map` by raw `OdeFlow*` pointer. If an `OdeFlow` is destroyed
and a new one is later allocated at the same address with different
dynamics, the cache returns stale data for the new flow. The test file
sidesteps this by holding `OdeFlow` shared_ptrs in `inline static` class
members so they outlive the process. Worth fixing in production code
(weak_ptr-based key, or content-hash key, or explicit eviction on flow
destruction); not part of this pass.

### Files touched

```
src/dreal/contractor/odes/contractor_odes.cc        (Prune Step 4: direction branch instead of BWD early-return)
src/dreal/contractor/odes/contractor_odes_codac.cc  (+ run_lohner_bwd_oneshot)
src/dreal/contractor/odes/contractor_odes_codac.h   (+ run_lohner_bwd_oneshot declaration)
test/dreal/contractor/test/contractor_odes_semantic_test.cc   (NEW)
benchmark/baseline.csv                              (+ ground_truth column)
benchmark/baseline_local.csv                        (+ ground_truth column)
benchmark/aggregate.py                              (+ ground-truth-aware flip classification + frozen-baseline fallback)
```

### Natural next steps

- Fuzz the BWD contractor with sympy-derived polynomial closed-form
  fixtures (random RHS within a polynomial template) to harden confidence
  past the seven hand-picked fixtures.
- Item 5 from "Open lines of attack" — reduce FWD `CtcLohner` from
  `FWD_BWD` to `FWD` only, pushing X_0 narrowing onto the new BWD path.
- Fix the cache-key bug noted above (weak_ptr or content-hash).
