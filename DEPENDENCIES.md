# Forked Dependency Notes

These are the two forked/modified external libraries that dReal4 depends on. Both are unmaintained forks of old versions and cannot simply be replaced with upstream releases.

---

## IBEX (`ncsys-lab/ibex-lib`)

**Upstream**: `ibex-team/ibex-lib` (last merged: v2.8.8 circa 2019)  
**Local path**: `/Users/kunalsheth/Documents/new_dreal/ibex-lib`  
**Role**: Interval constraint propagation. Provides `IntervalVector`, `Function`, `HC4Revise` (the forward-backward contractor), and `CtcPolytopeHull`.

### Why We Forked

The upstream project is effectively dormant and does not support modern compilers or Apple Silicon. Additionally, we needed a performance-critical API change to `Function::backward` that would never be accepted upstream.

### Changes Made

#### 1. Ported to Modern C++ / C++17 (Kunal, Jan 2025)
The upstream code used `using namespace std;` throughout, including in lexer/parser files. This broke compilation under C++17 because Bison-generated parsers emit code in global scope where namespace pollution causes ambiguity errors. The fix was sweeping — 193 files had to be updated to use explicit `std::` prefixes.

- `1c18a60b` — Remove `using namespace std` across all source files
- `d59d2e3a` — Remove from lexer/parser specifically
- `95e85666` — Fix `apply()` call in `parser.yc` that was silently ambiguous post-C++11 (`ibex::parser::apply` required explicitly)

#### 2. Upgraded `gaol` + `mathlib` for Apple Silicon / x86 Rosetta (Kunal, Jan 2025)
IBEX's interval arithmetic backend is `gaol` (v4.2.0), which predates Apple Silicon entirely. The FPU control word management (`reset_fpu_cw`, `get_fpu_cw`) used `unsigned short int` — the x86 16-bit FPU control word format — which is wrong on ARM64/Rosetta.

- `f0e35027` — Swapped `gaol-4.2.0` for `gaol-4.2.3` and `mathlib-2.1.0` for `mathlib-2.1.1`. These were the last releases from the gaol author (downloaded Jan 2025 from frederic.goualard.net) and had partial ARM64 support.
- `fc986657` — Patched `gaol`'s `configure.ac` to recognize `x86_64-apple-*` and `arm-apple-*` targets, and changed FPU state save/restore from `unsigned short int` to `fenv_t`. Also switched `gaol_common.cpp` to use `default_fpu_cw()` instead of `reset_fpu_cw(GAOL_FPU_MASK)` which did not work correctly on x86-under-Rosetta.
- `4f845aa3` — Fixed `fenv.h` call signatures for x86 (ARM vs x86 have different `fegetenv`/`fesetenv` conventions).
- `059d1fe7` — Added `gaol::init()` call in the IBEX wrapper; without it, gaol's FPU state was uninitialized.
- `be485777` — Force `CMAKE_OSX_ARCHITECTURES=x86_64` in IBEX's own `CMakeLists.txt` (mirrors the same setting in dReal4's top-level CMake).

#### 3. Bug Fixes in Interval Math (Kunal, Jan 2025)
Two correctness bugs were found in the `gaol` wrapper, confirmed by cross-referencing the older `dreal-deps/ibex-lib` fork:

- `ebc65b84` — `Interval::log`: the guard `x.ub() <= 0` was wrong; `log(0)` is `-∞` (valid), so it should be `x.ub() < 0`. `Interval::pow(x, d)`: `gaol::pow` requires an interval second argument, not a `double`; was passing `d` directly which caused incorrect rounding.

#### 4. Performance: Callback on `Function::backward` (Kunal, Feb 2025)
The HC4 backward contractor (`Function::backward` / `HC4Revise::proj`) previously returned a new `IntervalVector` by modifying a box copy internally, then copying back changed values — an expensive operation for large boxes.

- `edbd8159` — Null out `_grad` (Gradient) allocation in `Function::init`. dReal never calls the gradient; skipping its construction saves significant memory and build time per `Function`.
- `4d61b841` — Added an optional `std::function<void(int index, old_value, new_value)>` callback to `Function::backward` and `ExprTemplateDomain::read_arg_domains`. The contractor can now observe which variables actually changed rather than comparing the whole box. **Note**: Both commits are prefixed "TMP" — this was an active optimization experiment at time of commit, not a finalized interface.

#### 5. Soonho Kong's Additions (2015–2016, original dReal4 era)
These predate Kunal's work and are part of the original dReal4 fork baseline:

- `6791a4b0` — Implement `Gradient::atan2_bwd` (backward pass for `atan2`). Upstream IBEX lacked this, so formulas using `atan2` could not be differentiated.
- `59642a0a` — Guard against empty Jacobian → empty Hansen matrix (crash fix).
- `5ede6e7a` — `ExprSimplify::visit(ExprChi&)` — handle the `chi` (if-then-else on intervals) node type that dReal adds to IBEX's expression language.
- Various: suppress compiler warnings (unused params, signed/unsigned, overloaded-virtual) — necessary to build cleanly with the compilers used on dReal's CI.

---

## CAPD (`capdDynSys-4.0`)

**Upstream**: CAPD group at Jagiellonian University (last snapshot: 2016-08-15)  
**Local path**: `/Users/kunalsheth/Documents/new_dreal/capdDynSys-4.0`  
**Role**: ODE integration and rigorous enclosures of ODE trajectories. Used by `contractor_capd_*` to verify that a candidate trajectory satisfies the differential constraints in `ForallT`/`Integral` formulas.

### Why We Forked

CAPD 4.x uses autotools, is x86-only, and was frozen in 2016. It uses C++03/C++11 features that were deprecated or removed in C++17. It also had interval arithmetic semantics that were too strict for use inside a delta-complete solver (throwing exceptions on operations that should yield `[-∞, +∞]`). This repository is Soonho Kong's fork from the original dReal4 project (circa 2015), with one additional C++17 fix by Kunal.

### Changes Made

#### 1. Extended Interval Semantics (Soonho Kong, 2015)
CAPD's default interval arithmetic throws exceptions on operations where the mathematical result is unbounded or partially undefined. A delta-complete solver needs to continue with `[-∞, +∞]` in these cases instead of aborting.

- `9070e7e` — Division by zero: instead of throwing `IntervalError`, return `[-∞, +∞]` (with special case: `[0,0] / [0,0]` returns `[0,0]`).
- `c942a5d` — `sqrt([a,b])` where `a < 0`: upstream threw if `leftBound() < 0`. Changed to throw only if `rightBound() < 0` (truly empty domain), and clamp `leftBound` to `max(0, a)` for the lower computation.
- `3420eae` — Switched FILIB's interval mode from `i_mode_normal` to `i_mode_extended`. Extended mode propagates `[-∞, +∞]` through divisions-by-zero rather than raising SIGFPE.

#### 2. Performance / Correctness Fixes (Soonho Kong, 2015)
- `26bc504` — Wrap `checkInterval` validation calls in `#ifdef __DEBUGGING__`. These are O(1) but called on every interval construction; gating them behind a debug flag removes overhead in release builds.
- `fa6b46c` — Simplify `checkInterval` in `IntervalError.h` to avoid a string-buffer use-after-free.
- `754ec17` — Fix `IntervalError::what()` to use a member string buffer (`m_temp`) instead of a temporary — the old version returned a pointer to a destroyed local.
- `8f4cc35` — Mark `Interval` copy constructor `noexcept` (C++11). Required for `Interval` to be usable in `std::vector` without disabling move optimization.
- `12dd4d4` — Mark `Container` (the vector/matrix backing store) move constructor `noexcept`. Same reason — enables efficient STL container usage.

#### 3. C++17 Compatibility (Kunal Sheth, Sep 2025)
- `54145cf` — Replace all `throw(SomeException)` dynamic exception specifications with `noexcept(false)`. Dynamic exception specifications were deprecated in C++11 and **removed** in C++17; GCC 14 (used on the CentOS/Sherlock cluster) rejects them as hard errors.

#### 4. Build System / Infrastructure (Soonho Kong, 2015–2016)
- `0f66094` — Disable `mpcapd` (multi-precision CAPD extension) in `configure.ac`. dReal does not need multi-precision intervals and mpcapd has additional dependencies (MPFR, etc.).
- `229a176` — Accept Clang 3.x as C++11-compatible in the compiler detection script (the upstream check was too conservative and rejected valid compilers).
- `20267a7` — Update `boost.m4` to a newer version to find Boost on modern systems.
- `edc1d8c` — Fix broken symlinks for missing source files.

---

## Implications for Future Work

- **Do not upgrade either library to a newer upstream version** without re-applying all the above patches. There is no upstream that incorporates these changes.
- **The "TMP" prefix on the IBEX `Function::backward` callback commits** (`4d61b841`, `edbd8159`) signals that this interface was experimental. The dReal4 contractor code uses it but it was not considered final API.
- **CAPD is autotools-based**; CMakeLists.txt in dReal4 downloads and builds it via FetchContent using a custom configure/make invocation. Changes to the CAPD source require running autotools (`autoreconf`) inside the CAPD tree before they take effect.
- **gaol is embedded inside IBEX** as a patch+tarball (`interval_lib_wrapper/gaol/3rd/`). The gaol source is not checked in directly — it is applied as a patch at IBEX build time. Any further gaol changes must be made to `gaol-4.2.3.all.all.patch`, not to a checked-out gaol tree.
