# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

dReal4 is a delta-complete SMT solver for nonlinear arithmetic over the reals. It takes SMT-LIB2 (`.smt2`) or Delta-Real (`.dr`) formatted formulas and checks satisfiability up to a precision parameter delta. The `cav26` branch holds pattern-matching/lemma-reuse research; `upgrade-ibex` (current) source-builds IBEX from the `ncsys-lab/ibex-lib@dreal-perf-patches` fork and uses CAPD as the sole ODE backend.

## Build

**Prerequisites** (macOS — ARM or x86 Homebrew; Rosetta no longer required):
- bison, flex, gmp, cadical (install via `/opt/homebrew/bin/brew` on Apple Silicon)
- CMake source-builds IBEX from `https://github.com/ncsys-lab/ibex-lib.git@dreal-perf-patches` (override the URL via `-DIBEX_GIT_REPOSITORY=file:///path/to/ibex-fork` for local-dev iteration against an unpushed checkout) and builds CAPD from source automatically

**Full build** (first time — creates `gcc_build/`):
```bash
./FULL_BUILD.sh
```

**Incremental build** (subsequent builds):
```bash
./BUILD.sh
```

Both scripts build target `dreal4` with `-j8`. The binary is at `gcc_build/dreal4`.

**macOS note**: arm64-native builds work without Rosetta — IBEX `971f8eb0` (March 2025) added arm64-gaol support, and CAPD master uses its own `DoubleRounding` (via `CAPD_INTERVAL_TYPE=NATIVE`) so FILIB is not pulled in. The old `rosetta_cmake.sh`/`rosetta_lldb.sh` wrappers are vestigial.

**Docker** (Linux hermetic verification via `Dockerfile.dreal_ubuntu`):
```bash
# The container's IBEX source-build clones from the GitHub fork; the build
# sandbox needs outbound HTTPS. For local-dev against an unpushed checkout,
# pass --build-arg or edit IBEX_GIT_REPOSITORY in CMakeLists.txt before build.
docker build -f Dockerfile.dreal_ubuntu -t dreal-linux-verify .
cat query.smt2 | docker run --rm -i dreal-linux-verify ./dreal4 --in --model
```
Ubuntu 24.04 + clang-18 + bison-3.8.2 + flex-2.6.4 + cadical-3.0.0 + GMP 6.3.0 (all source-built inside the container). Docker on macOS may have `-j` filesystem bugs — reduce parallelism if you see bad file descriptor errors.

## Running Tests

Tests are built as `dreal4_cmake_test`. Build and run:
```bash
cd gcc_build
cmake --build . --target dreal4_cmake_test -j8
ctest                                      # run all tests
./dreal4_cmake_test --gtest_filter="*Box*" # run a single test by name pattern
```

Test sources live under `test/dreal/` mirroring `src/dreal/` structure (e.g., `test/dreal/util/test/box_test.cc`).

**Known flaky tests (ignore until fixed):** three tests fail spuriously and are unrelated to solver correctness — a clean run is "569/572 with only these failing":
- `IfThenElseEliminatorTest.NestedITEs` and `IfThenElseEliminatorTest.ITEsInForall` — the ITE-elimination golden strings hard-code auxiliary-variable names (`ITE0`, `ITE1`, …) but the underlying counter is a process-global that other tests increment, so the expected vs actual names drift (`ITE0` vs `ITE3`) depending on test/registration order. A test-isolation bug, not a preprocessing bug.
- `Timer.Test1` — timing-threshold assertion that fails under load/scheduling jitter.

These fail identically on a pristine tree (verified by stashing local changes), so they do not indicate a regression. When validating a change, confirm the failure set is exactly this trio.

## Running the Solver

```bash
./gcc_build/dreal4 --precision 0.001 --produce-models input.smt2
./gcc_build/dreal4 --in --model          # read from stdin
```

Key flags: `--precision <delta>`, `--produce-models`, `--logic <QF_NRA|QF_NRA_ODE>`, `--verbose`.

## Architecture

### Solving Pipeline

1. **Parsing** (`src/dreal/smt2/`, `src/dreal/dr/`): Flex/Bison grammars generate parsers at build time into `cmake-build-*/parsers/`. Parsed commands go through a `Driver` → `Context`.

2. **Preprocessing** (`src/dreal/util/`): Formulas are put through if-then-else elimination (`if_then_else_eliminator`), predicate abstraction (`predicate_abstractor`), and Tseitin CNF conversion (`tseitin_cnfizer`) before solving.

3. **SAT Layer** (`src/dreal/solver/sat_solver.cc`): CaDiCaL solves the abstracted Boolean formula. Two logic modes: interval-based (`sat_solver_interval_logic.cc`) and model-based (`sat_solver_model_logic.cc`).

4. **Theory Layer** (`src/dreal/solver/theory_solver.cc`, `icp*.cc`): When SAT produces an assignment, the theory solver validates it via Interval Constraint Propagation (ICP). Both sequential (`icp_seq`) and parallel (`icp_parallel`) implementations exist.

5. **Contractors** (`src/dreal/contractor/`): ICP works by iterating contractors — algorithms that shrink variable domains. Key contractors:
   - `contractor_ibex_fwdbwd`: IBEX forward-backward propagation (main workhorse)
   - `contractor_ibex_polytope`: Polytope relaxation
   - `contractor_fixpoint`: Runs a contractor to fixpoint
   - `contractor_seq` / `contractor_join`: Sequential and disjunctive composition
   - `contractor_ode_lohner`: ODE contractor wrapping CAPD's order-10 `IOdeSolver` + `ITimeMap` (`contractor_odes_capd.{h,cc}`). The trivial-flow short-circuit (every RHS is literal 0) bypasses CAPD and just intersects X_0 ∩ X_t. `--visualize` produces step-by-step CAPD enclosures via `run_capd_trace`. CAPD divergence on a Prune call silently skips narrowing for that call. The previous Codac/CAPD gated hybrid was retired; see `CODAC_MIGRATION.md`.

6. **Pattern Matching / Lemma Generation** (`src/dreal/util/pattern_matching/`): CAV26 feature — generates lemmas from previously solved subproblems to prune future search via `substitution_tree` and `lemma_generator`. Randomization in `substitution_tree.cc` iteration is a recent optimization.

### Key Data Structures

- **`Box`** (`src/dreal/util/box.h`): The central solution type — a map from `Variable` to `ibex::Interval`. Represents both the current search space and satisfying witnesses.
- **`Variable` / `Expression` / `Formula`** (`src/dreal/symbolic/`): Vendored from Drake. Symbolic algebra layer with hash-consing.
- **`ContractorStatus`** (`src/dreal/contractor/contractor_status.h`): Carries the current `Box` plus metadata through contractor composition.

### Vendored Third-Party Code (`src/third_party/`)

Do not modify these unless necessary — they are external projects vendored in:
- `com_github_robotlocomotion_drake/`: Drake's symbolic expression library
- `com_github_khizmax_libcds/`: Lock-free concurrent data structures
- `com_github_progschj_threadpool/`: Thread pool for parallel ICP
- `com_github_pinam45_dynamic_bitset/`: Bitset for variable index sets
- `com_github_dreal-deps_picosat/`: PicoSAT (legacy, mostly unused)

### Auto-downloaded Dependencies

CMake fetches and builds at configure time:
- **IBEX** (`ncsys-lab/ibex-lib@dreal-perf-patches`, source-built via ExternalProject from `${IBEX_GIT_REPOSITORY}` defaulting to `https://github.com/ncsys-lab/ibex-lib.git`; override to `file:///path/to/ibex-fork` for local-dev iteration). 7 surgical patches on top of mainline `ibex-team/ibex-lib` (lazy-grad, backward callback, parser.yc ADL fix, mathlib arm64-Linux, plus 3 callback audit fixes for vector/matrix args, reference aliasing, and `EmptyBoxException` precision); see `../ibex-fork/MIGRATION.md`. Installed into `gcc_build/ibex-install/`.
- **CAPD** (`CAPDGroup/CAPD@b353e170`, master pin for in-development `6.1.0`, `CAPD_INTERVAL_TYPE=NATIVE`): Built from source via ExternalProject into `gcc_build/capd-install/`. Native intervals (CAPD's own `DoubleRounding`) skip FILIB and work on ARM64.
- **fmt**, **spdlog**, **nlopt**: Via FetchContent
- **GTest**: Via FetchContent

Codac and Eigen3 are no longer dependencies. See `DEPENDENCIES.md` for the current stack and `CODAC_MIGRATION.md` for the historical migration narrative.

### Vendored PicoSAT

`com_github_dreal-deps_picosat/` is legacy and effectively unused. The SAT solver was upgraded to CaDiCaL early in `main`'s history (after a brief revert back to PicoSAT confirmed CaDiCaL was the right choice). Don't touch PicoSAT code.

## Branch Lineage and History

This project started as a CMake port of the original dReal4 (which used Bazel). The `main` branch holds the stable base; research branches layer experiments on top.

**`main`**: Foundation work — CMake build, IBEX upgrade (2.7.4 → 2.8.9), PicoSAT → CaDiCaL upgrade, core performance fixes (lambda callbacks to avoid `Box` copies, O(N²) explanation matching fix, `Variable::get_id()` inlining, FPU rounding mode guards), and the initial trie-based pattern matching data structure.

**`fmcad25-experiments`**: First serious research branch. Added:
- The core "learned clause" pattern matching idea: when the theory solver produces an explanation (UNSAT witness), pattern-match it against known lemmas and inject reuse into the SAT solver via `CaDiCaL::Learner`.
- A neural-network-based "WORTH IT" heuristic for deciding when pattern matching pays off (later ripped out in `cav26` in favor of simpler thresholds).
- `FMCAD25_MODE_*` compile-time macros for benchmarking modes.
- The concept of "underconstrained models" in `context_impl.cc`.
- Application target: analog/mixed-signal circuit verification (SAR-ADC benchmarks, ASPLOS circuit problems).

**`tacas26-odes`**: Extends `fmcad25-experiments` with full ODE support:
- Added `Integral` and `ForallT` AST node types to the Drake symbolic library.
- Parser backward-compatibility with dReal3's ODE input format (`.dr` files using `d/dt[x] = ...` syntax).
- Ported the CAPD contractor from dReal3 and wired it into the existing contractor framework.
- ODE symbol table in the parser/driver; `LookupOde(double)` for dReal3 compat.
- `--visualize` flag and JSON flow dumps for ODE trajectory visualization.

**`upgrade-ibex`** (current): The post-Codac-elimination architecture, on top of `tacas26-odes`. IBEX is source-built from `ncsys-lab/ibex-lib@dreal-perf-patches` (7 surgical patches catalogued in `../ibex-fork/MIGRATION.md`). CAPD master is the sole ODE backend.
- ODE contractor in `contractor_odes.cc`: forward via `run_capd_fwd` (CAPD `IOdeSolver` order-10 (tunable `kCapdTaylorOrder`), forward integration of `f(x)`, terminal intersection with X_t, backward sweep from narrowed X_t via `-f(x)` for joint narrowing of X_0); backward via `run_capd_bwd` (one-shot backward image via the cached `-f(x)` IMap); `run_capd_trace` for `--visualize`.
- Per-flow `CapdOdeCache` holds both the forward `f(x)` and the negated `-f(x)` `capd::IMap` objects, built once and reused per flow. `IOdeSolver` instances are constructed per-call because they carry mutable step state. The trivial-flow short-circuit bypasses CAPD entirely when every RHS is literal 0.
- `contractor_ibex_fwdbwd::Prune` uses the IBEX fork's `Function::backward` callback patch to populate the output bitset directly without a before/after snapshot.

**`cav26`**: Replaces the old trie-based pattern matcher with a fundamentally different approach:
- **DeBruijn canonicalization** (`debruijn_canonical.cc`): Converts AST terms to a canonical alpha-equivalent form using De Bruijn indices, so structurally identical formulas up to variable renaming hash the same.
- **`substitution_tree.cc`**: Efficient alpha-bijection checking. `substitutions_map` "forward"/"backward" terminology refers to the two directions of the bijection being maintained during matching.
- **Flat-vector `substitutions_map`**: Replaced `std::unordered_map` with a flat vector for cache-friendliness.
- **Symmetry filtering** (`context_impl.cc`): CAV26's main contribution — filters redundant lemmas at the lemma level using variable symmetry information. `CAV26_NOT_PURE_ANY` tags mixed symmetries.
- **Dummy Variables**: Variables with negative IDs are internal/dummy variables used by the pattern matcher; not real solver variables.
- Removed all heuristic infrastructure (`predicate_heuristic.cc`, `pattern_matching_heuristic.cc`) that was in earlier branches.
- PM thresholds and timeouts are now CLI-configurable (`--drpm-max-size`, `--drpm-max-time`) instead of compile-time.
- Randomized iteration order in `substitution_tree.cc` to avoid biasing towards early-indexed variables when a timeout interrupts matching.

## Benchmarking

Run `/benchmark` after every meaningful code change. This is the primary regression-detection mechanism during active development — run it frequently, not just before commits. Suggest it proactively at natural breakpoints even if the user doesn't ask.

**Infrastructure** (`benchmark/` directory):
- `baseline.csv` — frozen DRPM_0L reference times for 102 benchmarks (good_benchmarks.csv subset)
- `state.json` — persistent anomaly/exceptional tracker; updated automatically each run
- `run_batch.sh` — parallel runner: reads TSV from stdin, runs each with `gtime -v -o` and `timeout 300`
- `select.py` — picks 8 random benchmarks + all current anomalies; outputs TSV (csv_name TAB filepath)
- `parse_results.py` — parses gtime output + solver stdout into `summary.csv`
- `aggregate.py` — compares vs baseline, flags regressions/exceptional, updates `state.json`
- `results/` — per-run output directories (gitignored)

**Skills** (invoke from Claude Code prompt):
- `/benchmark` — runs ~8-12 benchmarks in parallel, spawns a Haiku subagent to interpret results, reports back 2-4 sentence summary with regression/exceptional counts
- `/benchmark-baseline` — runs ~30 benchmarks to establish a fresh local baseline (use before branch merges or when exceptional list grows stale)

**Thresholds**: regression if PAR2 time >1.5× baseline (PAR2 = actual time if solved, 2× timeout = 600 s if TIM/OOM/ERR); exceptional if PAR2 time <0.6× baseline. Correctness flips (SAT↔UNSAT) are always escalated immediately regardless of timing.

**Benchmark sources** (raw `.smt2` files, not in this repo):
- `~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/` — github_oct5_ family
- `~/Documents/new_dreal/nraode_to_nra/VNAMSCwI_satoct11/rolled/` — tacas_c2e2_ family
- `~/Documents/new_dreal/AMS-verification-bundle-of-sticks/saradc/rolled/` — 1mhz_ family

**Manual invocation** (if not using the skill):
```bash
python3 benchmark/select.py | bash benchmark/run_batch.sh benchmark/results/run_$(git rev-parse --short HEAD)_$(date +%s)
python3 benchmark/parse_results.py <results_dir>
python3 benchmark/aggregate.py <results_dir>
```

## Verification discipline

When reporting "tests pass" or "build green," confirm which artifact (commit, build dir, container image) the verification actually ran against. The trap to avoid: ctest reports a pass against `build-old-pin/` (a proven baseline) while changes are on a new branch with a fresh build dir — the report says "tests pass" but the new code was never exercised.

Always run the verification command against the new artifact explicitly and cite the build dir / commit / image tag in the report (e.g. "ctest against `build-final/` at HEAD `<sha>`: 537/539").

## Key Design Notes

**dReal3 backward compatibility is intentional.** The DR parser (`src/dreal/dr/`) handles the older dReal3 ODE syntax. Don't break this.

**`auditor.cc`** (`src/dreal/solver/auditor.cc`) is a development/verification tool. It reprints learned lemmas in dReal3-compatible format so they can be re-checked by dReal3 independently. It is not part of the core solving loop.

**FPU rounding mode** (read this before touching any interval/ODE code — the failure mode is extremely sneaky): the solver controls the FPU rounding mode explicitly via `RoundingModeGuard` (`src/dreal/util/rounding_mode_guard.h`), which sets a mode on construction and restores the *previous* mode on destruction. Two interval backends with **conflicting** ambient-mode expectations coexist:

- **gaol** (IBEX's interval backend) is sound only under **`FE_UPWARD`**. Under any other mode its directed rounding inverts (`lo > hi`), so an inexactly-FP-representable subexpression collapses to an *empty* interval.
- **CAPD** (ODE backend) expects **`FE_TONEAREST`**; its `DoubleRounding` sets directed modes per-op and restores nearest. Crucially, **linking CAPD leaves the process FPU in `FE_TONEAREST`** (its static init / `roundNearest()`), so there is no longer a safe ambient mode to assume.

Invariant: **every gaol↔CAPD boundary must establish its mode explicitly — never rely on the ambient FPU state.** gaol-using code needs `FE_UPWARD`; CAPD-using code guards `FE_TONEAREST`.

**Phase-hoisting + capability token (the current design).** `FE_UPWARD` is *not* established per-`Prune` anymore — that paid an `fesetround` (pipeline-serializing) on every contractor invocation in the ICP hot loop. Instead it is established **once per ICP phase** by an `UpwardRoundingScope` (RAII, in `rounding_mode_guard.h`) at each phase entry — `IcpSeq::CheckSat` (before the loop) and each `IcpParallel` worker (FPU mode is thread-local, so every worker mints its own). The scope's `token()` yields a zero-size `UpwardRounding` capability that is threaded as a parameter through `Contractor::Prune(ContractorStatus*, const UpwardRounding&)` and every override/combinator. The token is **the sole proof** a contractor may run: a gaol contractor cannot be pruned without a caller-supplied token, and `UpwardRoundingScope` is the only minter — so the FE_UPWARD invariant is compile-time-enforced and direct callers (e.g. tests) must establish a scope. The pure-gaol leaves (`ContractorIbexFwdbwd::Prune`, `ContractorIbexPolytope::Prune`) no longer guard; they `DREAL_ASSERT_ROUNDING(FE_UPWARD)` to verify the inherited phase mode in Debug. CAPD code still guards `FE_TONEAREST` internally (`contractor_ode_lohner::Prune` / `generate_trace` / the CAPD cache build); because CAPD calls *ibex* invariant sub-contractors, it re-establishes a nested `UpwardRoundingScope` (`contractor_odes.cc`) to mint the token they require — the token makes that reentrant switch mandatory. The `brancher.cc` `DREAL_ASSERT_ROUNDING(FE_UPWARD)` only *asserts* the invariant; it inherits the phase mode.

The sneaky part: a wrong ambient mode does **not** crash or warn — it silently produces an inverted interval that empties the box, surfacing as a **false `unsat`** (delta-complete soundness is violated) only on formulas containing inexact constant arithmetic inline in inequalities (e.g. `(* 3.3 x)`, `(* C (pow 10 -N))`). It is invisible on exactly-representable values (`0.5`, `0.25`, `*1.0`). Regression test: `test/dreal/api/test/gaol_directed_rounding_false_unsat_test.cc`. Historical guards also live in `prefix_printer.cc`. Don't add floating-point/interval code without an explicit `RoundingModeGuard` for the backend it touches.

**Debug-only rounding assertions and the Debug gate.** `DREAL_ASSERT_ROUNDING(mode)` compiles to *nothing* under `NDEBUG` (it no longer even evaluates `fegetround()`), so it is free in release and safe to sprinkle densely at gaol/CAPD boundaries — it currently guards the entries of `ContractorIbexFwdbwd::Prune`, `ContractorIbexPolytope::Prune`, `Box::MaxDiam`, and the CAPD `contractor_ode_lohner::Prune`. `RoundingModeGuard` also maintains a debug-only `thread_local` shadow (`rounding_detail::g_rounding_shadow`) of the mode the guard stack expects; `DREAL_ASSERT_ROUNDING_CONSISTENT()` (placed at the gaol→CAPD boundary) asserts the live FPU register still matches that shadow, catching any `fesetround`/CAPD clobber that bypasses a guard — drift a plain guard cannot detect. Because these only run in a **Debug** build, `./rounding_debug_gate.sh` builds the Debug test target (`cmake-build-debug`) and runs the suite, failing on any rounding-assertion abort (the known-flaky `EXPECT` trio does not fail it). Run it in CI / before merges. **`.diam()`/`.mid()` are NOT mode-safe getters** — they run gaol directed-rounding FP, sound only under FE_UPWARD; only `.lb()`/`.ub()` are pure stored-value reads. Solve-time call sites go through `safe_diam()`/`safe_mid()` (`util/rounded_double.h`), which `DREAL_ASSERT_ROUNDING(FE_UPWARD)` and let the rounding lint forbid raw `.diam()`/`.mid()` elsewhere. (Legitimate exceptions, allow-listed: the SMT2 parser at parse time and model printing under FE_TONEAREST.)

**Typed directed-rounding doubles** (`util/rounded_double.h`): the scalar analog of the `UpwardRounding` token. Hand-written `double` arithmetic feeding an interval endpoint is the trap — `ibex::Interval(mid - half, mid + half)` is mis-rounded under *every* single mode (the lower endpoint is pulled inward → too-narrow box). `Exact`/`RoundedDown`/`RoundedUp` encode the rounding direction in the type; `make_sound_interval(RoundedDown lo, RoundedUp hi)` only accepts an outward-rounded pair, so the compiler checks it. `sub_down`/`add_up`/etc. compute the round-down direction via `roundDown(x) = -roundUp(-x)` under FE_UPWARD (no per-op `fesetround`) and require the `UpwardRounding` token. `Tighten` (`context_impl.cc`) uses this for its delta-box construction; `test/dreal/util/test/rounded_double_test.cc` verifies the directed rounding matches gaol's own enclosure.

**`filter_assertion` soundness**: There was a soundness bug where strict upper bounds were handled incorrectly due to a wrong `nextafter()` call. The `forward`/`backward` naming in `substitutions_map` also had a soundness bug that was fixed. Be careful around strict vs. non-strict inequality handling in contractors and the SAT interval logic.

**ODE performance baseline**: Pre-Codac-elimination benchmarking confirmed CAPD order-20 was at or below Codac CtcLohner runtime on all tested ODE benchmarks (cardiac, prostate, bouncing-ball families). See `CODAC_MIGRATION.md` for the historical benchmark tables. The `--capd-t-gate` / `--capd-ndim-gate` CLI flags have been removed — they were only meaningful under the old Codac/CAPD gated hybrid.

**Benchmarking instrumentation**: Several `std::cerr` prints and JSON dumps exist specifically for benchmarking runs. Log levels (TRACE/DEBUG/INFO) are tuned so that `--verbose` (DEBUG) is useful for development without flooding output on large queries. TRACE is for deep debugging only.
