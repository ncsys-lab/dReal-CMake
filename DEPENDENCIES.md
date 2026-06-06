# Dependency Notes

This document describes the external libraries that dReal4 depends on, including the
rationale for choosing specific versions and the changes made from upstream.

---

## IBEX (`lebarsfa/ibex-lib`)

**Version**: `ibex-2.8.9.1` tag  
**Build**: ExternalProject, installs into `gcc_build/ibex-install/`  
**Role**: Interval constraint propagation. Provides `IntervalVector`, `Function`,
`HC4Revise` (the forward-backward contractor), and `CtcPolytopeHull`.

This replaces the old `ncsys-lab/ibex-lib` fork (based on ibex-2.8.8).

### Why This Fork

`lebarsfa/ibex-lib` is the actively maintained IBEX fork that Codac is designed to
work with. It has native ARM64 support (no Rosetta required), is kept in sync with
upstream IBEX bug fixes, and uses a standard CMake build system (no autotools).

The old `ncsys-lab/ibex-lib` fork required heavy patches (ARM64 FPU fixes, C++17
namespace fixes, a custom `Function::backward` callback, and a gradient null-out
hack) and had no active maintainer. See the git history of `ncsys-lab/ibex-lib` for
the full patch history.

### Changes from Upstream lebarsfa/ibex-lib

None. We use the upstream lebarsfa fork directly without any local patches. The old
custom patches are no longer needed:
- The C++17 namespace fixes are present in lebarsfa/ibex-lib upstream.
- The ARM64 FPU fixes are handled by lebarsfa and Codac natively.
- The `Function::backward` callback is replaced by a pre/post box comparison in
  `contractor_ibex_fwdbwd.cc` (see Phase 2 in `CODAC_MIGRATION.md`).
- The gradient null-out hack is dropped; upstream IBEX always allocates the gradient
  and the performance impact is acceptable.

---

## Codac (`codac-team/codac`)

**Version**: `v2.0.2`  
**Build**: ExternalProject, installs into `gcc_build/codac-install/`; built `WITH_CAPD=OFF`  
**Role**: ODE interval integration (Lohner method via `CtcLohner`), plus `CtcFunction`
(HC4 forward-backward) from the Codac 2.x API.

This replaces the old `ncsys-lab/capdDynSys-4.0` CAPD fork.

### Why Codac

CAPD 4.x was a 2016 snapshot that required autotools, was x86-only, and depended on
both FILIB and CAPD's own interval arithmetic (separate from IBEX). Codac v2 is
actively maintained, supports ARM64 natively, provides a cleaner C++ API for ODE
integration (`CtcLohner`), and runs on top of IBEX (no separate interval arithmetic
backend needed).

`WITH_CAPD=OFF` disables Codac's optional CAPD backend (for older CAPD-based ODE
methods). Codac's own `CtcLohner` implementation (Lohner/Picard iteration) does not
need CAPD.

### Changes from Upstream Codac

None. We use the upstream `codac-team/codac` at tag `v2.0.2` without local patches.

### ODE Contractor Status

`contractor_ode_lohner` (`src/dreal/contractor/odes/contractor_odes_codac.cc`) replaces
the old `contractor_capd_full`. Three entry points: `run_lohner_integration` (FWD via
`CtcLohner FWD_BWD`), `run_lohner_bwd_oneshot` (BWD via `LohnerAlgorithm(forward=false)`),
`run_lohner_trace` (`--visualize`). Sound; complete modulo `GlobalEnclosureError`
fallbacks on stiff dynamics. Known perf floor on ODE-heavy benchmarks (Codac
hardcoded to Taylor order 2 vs CAPD's order 20). See `CODAC_MIGRATION.md` for the
tuning history and the CAPD v6 ARM64 fallback patch.

---

## Other Dependencies

The following dependencies are unchanged from before the Codac migration:

| Dependency | Source | Role |
|---|---|---|
| fmt | FetchContent | String formatting |
| spdlog | FetchContent | Logging |
| nlopt | FetchContent | Nonlinear optimization (for branch/split heuristics) |
| GTest | FetchContent | Unit testing |
| picosat | Vendored (`src/third_party/`) | SAT solver (legacy, mostly inactive) |
| libcds | Vendored (`src/third_party/`) | Lock-free concurrent data structures |
| nlohmann/json | Vendored (`src/third_party/`) | JSON output for visualization |
| dynamic_bitset | Vendored (`src/third_party/`) | Bitset for variable index sets |
| Drake symbolic | Vendored (`src/third_party/`) | Expression/formula symbolic layer |
| ezoptionparser | Vendored (`src/third_party/`) | CLI option parsing |

---

## Migration History

For the detailed migration plan from `ncsys-lab/ibex-lib` + `ncsys-lab/capdDynSys-4.0`
to Codac, see `CODAC_MIGRATION.md`.
