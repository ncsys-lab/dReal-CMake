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
