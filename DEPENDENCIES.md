# Dependency Notes

This document describes the external libraries that dReal4 depends on, including the
rationale for choosing specific versions and the changes made from upstream.

---

## IBEX (`lebarsfa/ibex-lib`)

**Version**: `ibex-2.8.9.20250626` (prebuilt release zip)  
**Build**: Configure-time zip download, extracts into `gcc_build/ibex-install/`  
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
- The gradient null-out hack is dropped; upstream IBEX always allocates the gradient.
  **Performance impact is not acceptable** — the 2026-06-07 re-investigation
  (`CODAC_MIGRATION.md` § "Re-investigation 2026-06-07") shows ~65% of saradc wall
  time is spent in `ibex::Function::init` building a gradient that
  `Function::backward` (our only IBEX entry point on the fwdbwd hot path) never
  reads. Restoring the hack is "Open lines of attack" item 7 in
  `CODAC_MIGRATION.md` and requires switching `ibex_external` in `CMakeLists.txt`
  from prebuilt-zip to a source `ExternalProject_Add` with a small `PATCH_COMMAND`.

---

## Codac (`codac-team/codac`)

**Version**: `v2.0.2` (prebuilt release zip)  
**Build**: Configure-time zip download, extracts into `gcc_build/codac-install/`  
**Role**: ODE interval integration (Lohner method via `CtcLohner`), plus `CtcFunction`
(HC4 forward-backward) from the Codac 2.x API. Acts as the default/fallback ODE
backend in the gated hybrid; CAPD (see below) fires on long-horizon or high-dimensional
flows.

This replaces the old `ncsys-lab/capdDynSys-4.0` CAPD 4.x fork.

### Why Codac

CAPD 4.x was a 2016 snapshot that required autotools, was x86-only, and depended on
FILIB for directed rounding. Codac v2 is actively maintained, supports ARM64 natively,
provides a cleaner C++ API for ODE integration (`CtcLohner`), and runs on top of IBEX
(no separate interval arithmetic backend needed).

### Changes from Upstream Codac

None. We use the upstream `codac-team/codac` prebuilt binary at `v2.0.2` without
local patches.

### ODE Contractor Status

`contractor_odes_codac.cc` provides three entry points: `run_lohner_integration` (FWD
via `CtcLohner FWD_BWD`, `contractions=2`, `eps=0.1`, adaptive `n_steps`),
`run_lohner_bwd_oneshot` (BWD via `LohnerAlgorithm(forward=false)`), and
`run_lohner_trace` (`--visualize`). Sound; complete modulo `GlobalEnclosureError`
fallbacks on stiff dynamics. Codac's Taylor order is hardcoded to 2; CAPD
(see below) closes the perf gap on long-horizon / high-dimensional flows.

The trivial-flow short-circuit (`cache->trivial = true` when every ODE RHS is literal
zero) bypasses `CtcLohner` entirely — critical for planning benchmarks with thousands
of trivial modes.

See `CODAC_MIGRATION.md` for the full migration history, performance timeline, and
soundness analyses.

---

## CAPD (`CAPDGroup/CAPD`)

**Version**: master SHA `b353e170` (2026-05-18; in-development v6.1.0)  
**Build**: ExternalProject, builds from source into `gcc_build/capd-install/`  
**Build flags**: `-DCAPD_INTERVAL_TYPE=NATIVE -DCAPD_ENABLE_MULTIPRECISION=OFF -DCAPD_BUILD_ALL=OFF -DCAPD_BUILD_TESTS=OFF`  
**Role**: Second ODE backend — order-20 Taylor integration via `capd::IOdeSolver` +
`capd::ITimeMap`. Fires in the gated hybrid when a flow's time horizon exceeds
`--capd-t-gate` (default `5.0`) or its state-space dimension reaches `--capd-ndim-gate`
(default `6`). Falls back to Lohner on `GlobalEnclosureError`-equivalent failures.

### Why CAPD is back (v6 master, not v4.x)

Codac's `CtcLohner` is hardcoded to Taylor order 2. On long-horizon or
high-dimensional ODE flows (e.g. `quad`, `cardiac`, `prostate`), order-2 Taylor
enclosures widen quickly, driving more ICP bisections. CAPD's order-20 integrator
produces much tighter enclosures on those flows.

CAPD 4.x (the old `ncsys-lab/capdDynSys-4.0` fork) was x86-only due to an
unconditional FILIB dependency. CAPD master (in-development v6.1.0) exposes
`CAPD_INTERVAL_TYPE=NATIVE`, which causes `capdExt/CMakeLists.txt` to skip
`add_subdirectory(filibsrc)` entirely and use CAPD's own `DoubleRounding`
(`msr fpcr` assembly on ARM64, SSE on x86) as the interval backend. This closes
the ARM64 blocker.

### Why master SHA instead of a release tag

No `v6.1.0` release tag exists yet. The `CAPD_INTERVAL_TYPE` CMake switch landed
during the v6.1.0 development cycle (Jan 2025). SHA `b353e170` is pinned at a
known-good point after that switch. Bump only after verifying `CAPD_INTERVAL_TYPE`
plumbing is still intact in the new commit.

### Changes from Upstream CAPD

None. The `-DCAPD_INTERVAL_TYPE=NATIVE` build flag is a standard upstream CMake
option — no source patches required.

### Interval backend note

Consumers of `capd_imported` must define `__USE_NATIVE__` to select the matching
template instantiations. The `INTERFACE_COMPILE_DEFINITIONS "__USE_NATIVE__"` on
the `capd_imported` IMPORTED target in CMakeLists.txt propagates this automatically.

### Gating policy

`contractor_odes.cc::Prune` evaluates `use_capd = (t_ub > capd_t_gate) || (n_state_vars >= capd_ndim_gate)`. Gates are CLI-configurable:

- `--capd-t-gate ARG` (double, default `5.0`): CAPD fires when time horizon exceeds this.
- `--capd-ndim-gate ARG` (int, default `6`): CAPD fires when state dimension meets or exceeds this.
- Set `--capd-t-gate 1e18` (or both gates high) to disable CAPD entirely.
- Set both gates to `0` to force CAPD on every `Prune`.

The trivial-flow short-circuit takes precedence over the CAPD gate — flows whose
every RHS is literal zero skip both backends.

`kDefaultCapdTGate=5.0` and `kDefaultCapdNdimGate=6` are pre-measurement guesses;
see `CODAC_MIGRATION.md` § "Tuning gates" for the intended sweep workflow.

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
