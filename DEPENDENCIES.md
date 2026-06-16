# Dependency Notes

This document describes the external libraries dReal4 depends on, including the
rationale for choosing specific versions and the changes made from upstream.

> **Status (2026-06-08):** Codac is no longer used. IBEX is now source-built
> from the dReal team's fork (`ncsys-lab/ibex-lib@dreal-perf-patches`) and
> CAPD is the sole ODE backend. See `CODAC_MIGRATION.md` for the historical
> context and `../ibex-fork/MIGRATION.md` for the (minimal) patch catalog of
> the IBEX fork.

---

## IBEX (`ncsys-lab/ibex-lib`)

**Version**: branch `dreal-perf-patches` (7 patches on top of mainline `ibex-team/ibex-lib@65ed5877`).
**Build**: `ExternalProject_Add` source-build from `https://github.com/ncsys-lab/ibex-lib.git` (cache var `IBEX_GIT_REPOSITORY`, overridable to a `file://` path for local-dev iteration against `../ibex-fork`), installed into `gcc_build/ibex-install/`.
**Role**: Interval arithmetic + constraint propagation. Provides `IntervalVector`, `Function`, `HC4Revise` (the forward-backward contractor), polytope hull (`CtcPolytopeHull`), and the symbolic expression tree.

### Why a fork at all

The fork hosts seven surgical patches that aren't yet upstream (see `../ibex-fork/MIGRATION.md` for the full catalog):

1. **`function: lazy-init gradient`** (2 files, ~24 lines). `Function::init` no longer eagerly allocates the `Gradient` object; an inline `lazy_grad()` accessor builds it on first use. Profiling in `CODAC_MIGRATION.md` traced ~65% of `Function::init` wall time to this allocation when dReal never touches the gradient API.
2. **`Function::backward callback`** (4 files, ~16 lines). Adds an optional `std::function<void(int, const Interval&, const Interval&)>` argument to `Function::backward`. dReal's HC4 contractor (`contractor_ibex_fwdbwd.cc`) uses the callback to track narrowed variables without an `IntervalVector` snapshot.
3. **`parser.yc namespace fix`** (2 files, ~6 lines). Qualifies two unqualified `apply(...)` calls in the Bison-generated parser as `ibex::parser::apply(...)`, which avoids ADL ambiguities on modern toolchains (clang-18 + libc++ and GCC 13 + libstdc++).
4. **`mathlib: support aarch64/arm64 Linux`** (1 file, ~8 lines). Mainline mathlib's `CMakeLists.txt` doesn't recognize arm64 Linux as a supported platform; without this, `Dockerfile.dreal_ubuntu` fails to build on Apple Silicon (Docker defaults to native `linux/arm64`).
5. **`function: fire backward callback for non-scalar args`** (2 files, ~100 lines additive). Audit fix for patch #2: the non-scalar branch of `read_arg_domains` previously bypassed the callback for vector/matrix-typed function arguments. SMT theory-lemma generation relies on per-variable change events for soundness; this patch closes the gap via a new callback-aware `load()` overload in `ibex_TemplateDomain.h`.
6. **`function: copy old-value in backward callback to avoid alias`** (1 file, 1 line). Audit fix for patch #2: the scalar branch bound `old_value` as a const reference to a memory cell that the next line overwrote. Callers that retain `old_value` would see stale data. Copy by value.
7. **`HC4Revise: report partial narrowings on EmptyBoxException`** (1 file, ~7 lines). Audit fix for patch #2: when backward propagation throws `EmptyBoxException`, surface any narrowings that completed before the contradiction before calling `set_empty()`. Tightens theory-lemma precision for callers (dReal stays sound either way via its own empty-box handling).

Total fork diff vs mainline: 9 files, +166/−19 (excluding docs).

All seven are intended as upstream PRs. Once any/all merge, drop the corresponding commit; when all seven land, swap the `GIT_REPOSITORY` back to `ibex-team/ibex-lib` and delete the fork.

### Source-build invocation (from `CMakeLists.txt`)

`ExternalProject_Add(ibex_external)` configures with `-DINTERVAL_LIB=gaol -DLP_LIB=none -DBUILD_TESTING=0 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON`. arm64 native macOS is supported by mainline `971f8eb0` (March 2025) — no Rosetta needed. Linux source-build under clang-18 / GCC 13 needs the parser-fix patch from the fork, which is why the source pin is to the fork rather than mainline.

The `IBEX_GIT_REPOSITORY` CMake cache variable defaults to `https://github.com/ncsys-lab/ibex-lib.git`. Override with `-DIBEX_GIT_REPOSITORY=file:///path/to/ibex-fork` for local-dev iteration against an unpushed working tree, or `-DIBEX_GIT_TAG=<sha>` for testing alternate revisions.

---

## CAPD (`CAPDGroup/CAPD`)

**Version**: master SHA `b353e170` (2026-05-18; in-development v6.1.0)
**Build**: `ExternalProject_Add`, builds from source into `gcc_build/capd-install/`.
**Build flags**: `-DCAPD_INTERVAL_TYPE=NATIVE -DCAPD_ENABLE_MULTIPRECISION=OFF -DCAPD_BUILD_ALL=OFF -DCAPD_BUILD_TESTS=OFF`
**Role**: Sole ODE backend (after Codac removal). Order-20 Taylor integration via `capd::IOdeSolver` + `capd::ITimeMap`. Backward integration uses the negated `-f(x)` map stored alongside the forward map in the per-flow cache.

### Why CAPD master (not v4.x or v6.0)

CAPD 4.x (the old `ncsys-lab/capdDynSys-4.0` fork) was x86-only due to an unconditional FILIB dependency. CAPD master exposes `CAPD_INTERVAL_TYPE=NATIVE`, which causes `capdExt/CMakeLists.txt` to skip `add_subdirectory(filibsrc)` entirely and use CAPD's own `DoubleRounding` (`msr fpcr` on ARM64, SSE on x86) as the interval backend. No `v6.1.0` release tag exists yet, so we pin a master SHA at a known-good post-switch point. Bump only after re-verifying the `CAPD_INTERVAL_TYPE` plumbing in the new commit.

### Interval backend note

Consumers of `capd_imported` must define `__USE_NATIVE__` to select the matching template instantiations. The `INTERFACE_COMPILE_DEFINITIONS "__USE_NATIVE__"` on the `capd_imported` IMPORTED target propagates this automatically.

### Why CAPD-only (no more Codac fallback)

Codac's `CtcLohner` was order-2 Taylor and cheap on short horizons but widened out of usefulness on longer ones. The previous "gated hybrid" (Codac for short horizons, CAPD for long) added complexity to `contractor_odes.cc` and a `--capd-t-gate`/`--capd-ndim-gate` CLI surface. After removing Codac, CAPD's order-20 enclosures are tight enough that the gate is no longer beneficial; the trivial-flow short-circuit covers the truly degenerate cases.

A CAPD-based trace generator (`run_capd_trace`) preserves the `--visualize` flag's output: step-by-step CAPD `IOdeSolver` invocations produce per-slice enclosures matching the JSON shape of the now-removed `run_lohner_trace`.

---

## Other Dependencies

Unchanged from before the Codac migration:

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

Eigen3 has been dropped from the dependency list — it was required only by Codac.

---

## Build invocation

### macOS (arm64 native, no Rosetta)

```bash
cd dreal4-cmake
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
ctest --output-on-failure
```

Brew packages (auto-detected by `find_brew_package` in `CMakeLists.txt`): `bison`, `flex`, `gmp`, `cadical`.

### Linux (Ubuntu 24.04 + clang-18) via Docker

The `Dockerfile.dreal_ubuntu` is the hermetic Linux test harness. Because the IBEX source-build needs `../ibex-fork` available inside the container, the Docker build context must be the parent of `dreal4-cmake/`:

```bash
cd <parent containing dreal4-cmake and ibex-fork>
docker build -f dreal4-cmake/Dockerfile.dreal_ubuntu -t dreal-linux-verify .
```

The container builds CaDiCaL 3.0.0, GMP 6.3.0, Bison 3.8.2, and Flex 2.6.4 from source (matching the project's pinned versions), then runs `FULL_BUILD.sh` which configures + builds dreal4. End-to-end success is the Linux verification gate.

---

## Migration History

- `CODAC_MIGRATION.md` — the original migration off `ncsys-lab/ibex-lib` to Codac, the perf-regression analysis that motivated returning to a fork, and the final resolution.
- `../ibex-fork/MIGRATION.md` — divergence catalog of the IBEX fork (7-patch series).
