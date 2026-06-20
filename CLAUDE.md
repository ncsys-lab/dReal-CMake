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

**Footgun — a *new* test file needs a CMake reconfigure.** Test sources are gathered with `file(GLOB_RECURSE DREAL_TESTS …)` (CMakeLists.txt; no `CONFIGURE_DEPENDS`), so the file list is cached at configure time. A brand-new `.cc` is compiled/run only after a reconfigure: `cmake gcc_build` / `cmake cmake-build-debug` (or `FULL_BUILD.sh`, which reconfigures). The incremental paths do **not** reconfigure — `BUILD.sh`, a bare `cmake --build`, and `./rounding_debug_gate.sh` (it only invokes `ninja`) will silently skip the new file and report green *while never running it*. That is a verification trap (the gate "passes" but the new test never executed); reconfigure first, then confirm the suite count went up. Editing an *existing* test file is fine — ninja tracks it.

**Rounding-mode gate (`./rounding_debug_gate.sh`).** The FPU-rounding invariants are only checked in a Debug build (`DREAL_ASSERT_ROUNDING` compiles out under `NDEBUG`). This script builds the Debug test target (`cmake-build-debug`) and runs the suite, failing on any rounding-assertion abort; it runs `rounding_lint.py` (the static routing lint) first. Run it before merges. See the "FPU rounding mode" section under Key Design Notes.

**Known flaky tests (ignore until fixed):** three tests fail spuriously and are unrelated to solver correctness — a clean run is "585/588 with only these failing":
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
- **IBEX** (`ncsys-lab/ibex-lib@dreal-perf-patches`, source-built via ExternalProject from `${IBEX_GIT_REPOSITORY}` defaulting to `https://github.com/ncsys-lab/ibex-lib.git`; override to `file:///path/to/ibex-fork` for local-dev iteration). 11 surgical patches on top of mainline `ibex-team/ibex-lib` (lazy-grad, backward callback, parser.yc ADL fix, mathlib arm64-Linux, 3 callback audit fixes for vector/matrix args, reference aliasing, and `EmptyBoxException` precision, gaol `log`/`pow` soundness, the two gaol ARM64 rounding-cost levers — inline FPCR write #9 + batched nearest-region #10, plus #11 replacing `HC4Revise`'s `EmptyBoxException` control flow with a bool return-status so `__cxa_throw` unwinding leaves the backward hot path entirely); see `../ibex-fork/MIGRATION.md`. Installed into `gcc_build/ibex-install/`. `CMakeLists.txt` pins the fork sha (`cf3928c7`).
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

**`upgrade-ibex`** (current): The post-Codac-elimination architecture, on top of `tacas26-odes`. IBEX is source-built from `ncsys-lab/ibex-lib@dreal-perf-patches` (11 surgical patches catalogued in `../ibex-fork/MIGRATION.md`; `CMakeLists.txt` pins sha `cf3928c7`). CAPD master is the sole ODE backend.
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
- `baseline_odeexpr.csv` — odeexpr-family reference times (new format, `cpu_time_s` column); produced by `do_baseline_odeexpr.sh`, loaded by `aggregate.py` as the authoritative timing baseline for `odeexpr_*` rows
- `odeexpr.py` — single source of truth for the `odeexpr` family: `ODEEXPR_ROOT`, `FAMILY_WEIGHTS`, `family_of`, manifest-based `load_odeexpr_names`/`resolve_odeexpr`, and a `--all` TSV dump
- `state.json` — persistent anomaly/exceptional tracker; updated automatically each run
- `run_batch.sh` — parallel runner: reads TSV from stdin, runs each with `gtime -v -o`, `nice -n 1`, and `timeout 600`
- `select.py` — picks 8 **family-weighted** random benchmarks + all current anomalies; outputs TSV (csv_name TAB filepath)
- `parse_results.py` — parses gtime output + solver stdout into `summary.csv` (primary timing column `cpu_time_s` = user+sys; `wall_time_s` kept as reference/TIM backup)
- `aggregate.py` — compares vs baseline on CPU time, flags regressions/exceptional, updates `state.json`
- `results/` — per-run output directories (gitignored)

**Families & weighting**: four families, classified by name prefix in `odeexpr.family_of` — `saradc` (`1mhz_`), `github` (`github_oct5_`), `tacas` (`tacas_c2e2_`), and `odeexpr` (`odeexpr_<bench_id>`, the high-priority ode_expressivity set). `FAMILY_WEIGHTS = {odeexpr:6, saradc:3, github:2, tacas:2}` encodes "1 odeexpr ≡ 3 github ≡ 2 saradc ≡ 3 tacas". The weight drives weighted-without-replacement selection (odeexpr appears ~3× as often per item) and a `weighted_overall` PAR2 in the family comparison; odeexpr regressions are tagged with elevated `ODEEXPR`/`ODEEXPR-HIGH` priority so reports/skills lead with them. **Note: `OPTIMIZATION_LOG.md` (§Adopted/§Rejected) is all CAPD/ODE-path tuning and is orthogonal to odeexpr — odeexpr has no ODEs and runs the `IcpSeq → Fixpoint[IbexFwdbwd, Integer]` path. Three mechanical overheads addressed so far (post-Phase-2 profile is in `OPTIMIZATION_LOG.md`): (1) gaol's ARM64 interval-transcendental `fesetround` — cut ~8% aggregate by ibex-fork patches #9/#10 (inline FPCR write + toggle-batching), plus ~2pp from removing a spurious `NearestRoundingScope` in `is_integer`; (2) `EmptyBoxException` unwinding from HC4 prune-to-empty — was up to ~37% of CPU, **eliminated** by ibex-fork patch #11 (bool return-status), two benchmarks newly solved; (3) `ContractorIbexFwdbwd::Prune` stat timer overhead — `timer_pruning_.resume()/.pause()` were called unconditionally despite `stat.enabled() = false` at runtime (default log level `off`), costing 4–16% (worst on sin-heavy kuramoto where per-Prune work is short), **fixed** in `contractor_ibex_fwdbwd.cc`/`contractor_ibex_polytope.cc` by gating on `stat.enabled()`. Post-fix profile: `mach_continuous_time` 11.3% → 0%. Remaining floor: gaol transcendentals 30–45% (fundamental), HC4 forward+backward ~22%, ExpressionEvaluator ~6%, allocation ~5%. Open avenues A–E documented in `OPTIMIZATION_LOG.md` §"Open avenues (deferred)".**

**Timing & timeout**: the metric is **CPU time (user+sys)**, not wall clock — the machine is multi-tenant, so wall clock is noisy. Solver runs under `nice -n 1`; `timeout` stays wall-clock at **600 s** (TIM detection keys on exit code 124).

**Skills** (invoke from Claude Code prompt):
- `/benchmark` — runs ~8-12 benchmarks in parallel, spawns a Haiku subagent to interpret results, reports back 2-4 sentence summary with regression/exceptional counts
- `/benchmark-baseline` — runs all 43 odeexpr + ~10 each of the other three families to establish a fresh local baseline (use before branch merges or when exceptional list grows stale)

**Thresholds**: regression if PAR2 time >1.5× baseline (PAR2 = actual CPU time if solved, 2× timeout = 1200 s if TIM/OOM/ERR); exceptional if PAR2 time <0.6× baseline. Correctness flips (SAT↔UNSAT) are always escalated immediately regardless of timing.

**Benchmark sources** (raw `.smt2` files, not in this repo):
- `~/Documents/new_dreal/ode_expressivity/benchmarks/` — odeexpr family (43 self-contained `.smt2`, content-addressed via `manifest.json`; each sets its own `:precision`; no ground-truth `:status`)
- `~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/` — github_oct5_ family
- `~/Documents/new_dreal/nraode_to_nra/VNAMSCwI_satoct11/rolled/` — tacas_c2e2_ family
- `~/Documents/new_dreal/AMS-verification-bundle-of-sticks/saradc/rolled/` — 1mhz_ family

**Manual invocation** (if not using the skill):
```bash
python3 benchmark/select.py | bash benchmark/run_batch.sh benchmark/results/run_$(git rev-parse --short HEAD)_$(date +%s)
python3 benchmark/parse_results.py <results_dir>
python3 benchmark/aggregate.py <results_dir>
```

**Re-baseline the odeexpr family** (after the set is regenerated, or to refresh reference times):
```bash
bash benchmark/do_baseline_odeexpr.sh   # runs all 43 at 600 s → benchmark/baseline_odeexpr.csv
```

**Cross-solver comparison** (HEAD vs other dReal builds over the odeexpr set):
- `run_batch.sh` honors a `DREAL_BINARY` env override, so any alternate native build can be run over the same jobs:
  `DREAL_BINARY=/usr/local/bin/dreal4_cav26 bash benchmark/run_batch.sh <out_dir> /tmp/jobs.tsv`
- `run_dreal3.sh` — runs the set through dReal v3.16.12 in Docker (`dreal3:1.1`). dReal3 predates these benchmarks, so it needs a semantics-preserving input adaptation (prepend `(set-logic QF_NRA)`, strip the in-file `:precision` and pass it via `--precision`, strip `(get-model)`). **macOS gotcha**: the timeout is enforced *inside* the container (`timeout -s KILL 600 ./dReal`) — a host-side `timeout` around `docker run` only kills the docker client and leaves the container running in the VM as a zombie. Timing is the in-container CPU time (bash `time`); the SIGKILL exit (137) is normalized to TIM.
- `compare_solvers.py LABEL=summary.csv …` — joins per-solver summaries by benchmark and reports solve counts, SAT/UNSAT disagreements, solve-set deltas, and CPU-time speedups on commonly-solved benchmarks. Frozen reference results: `baseline_odeexpr_cav26.csv`, `baseline_odeexpr_dreal3.csv`; the rendered table is `odeexpr_solver_comparison.txt`. As of HEAD (arm64, upgraded IBEX/CAPD): identical solve-set + verdicts vs cav26 but ~2–3× faster; ~6–20× faster than dReal3, which also solves 2 fewer. No SAT/UNSAT disagreements among the three.

## Verification discipline

When reporting "tests pass" or "build green," confirm which artifact (commit, build dir, container image) the verification actually ran against. The trap to avoid: ctest reports a pass against `build-old-pin/` (a proven baseline) while changes are on a new branch with a fresh build dir — the report says "tests pass" but the new code was never exercised.

Always run the verification command against the new artifact explicitly and cite the build dir / commit / image tag in the report (e.g. "ctest against `build-final/` at HEAD `<sha>`: 537/539").

## Key Design Notes

**dReal3 backward compatibility is intentional.** The DR parser (`src/dreal/dr/`) handles the older dReal3 ODE syntax. Don't break this.

**`auditor.cc`** (`src/dreal/solver/auditor.cc`) is a development/verification tool. It reprints learned lemmas in dReal3-compatible format so they can be re-checked by dReal3 independently. It is not part of the core solving loop.

**FPU rounding mode** (read this before touching any interval/ODE/printing code — the failure mode is extremely sneaky): the FPU rounding mode is a thread-global resource with exactly two regimes, each handled by an identical, symmetric mechanism in `src/dreal/util/rounding.h` (a named RAII **Scope** that establishes the mode and mints a zero-size capability **token**; the token is threaded as compile-time proof into every mode-sensitive operation). The shared RAII primitive `rounding_detail::RoundingModeGuard` is **sealed** (private ctor; only the two scopes are friends), so no call site outside `rounding.h` ever names a rounding-mode constant. It also does **check-before-set**: the ctor reads the live mode to save it (a cheap `fegetround`) and issues the pipeline-serializing `fesetround` only when the requested mode actually *differs*. The dtor reads the live FPCR once and uses it for two things: (1) a **clobber tripwire** — if the mode the scope established did not survive to scope exit, some code changed the FPU mode out-of-band without a guard, a soundness hazard (gaol under the wrong mode → false `unsat`), so it `abort()`s (with a stderr diagnostic) in *every* build, not just Debug; and (2) a **live-based restore** that lands the FPU back in the caller's saved mode whether or not the body moved it, while still skipping the write when the mode is already correct. On ARM64 a `fesetround` (MSR FPCR) is ~5× a `fegetround` and ~11× when the value changes, so a nested scope re-establishing an already-active mode costs zero writes. Because both decisions read the *real* FPCR (never a stale shadow), the guard self-corrects against an out-of-band clobber. There are exactly **two sanctioned clobberers**, both handled with the `ExpectClobber` tag (which suppresses the tripwire for *that* scope and relies on the live-based restore to heal the mode, used as tightly as possible around the clobberer): (1) **CAPD** (its `DoubleRounding` leaves the FPU in a directed mode rather than restoring nearest — see below), contained inside the `run_capd_*` / `make_capd_ode_cache` adapters (`contractor_odes_capd.cc`); and (2) **ibex's interval `operator<<`** (it does internal directed rounding and likewise leaves a directed mode on return), contained at the two model-printing sites that invoke it — `Box::operator<<` (`util/box.cc`) and the smtlib2-compliant `PrintModel` (`smt2/driver.cc`). Both were previously masked by the old unconditional dtor restore; the always-on tripwire surfaced them (regression tests: `BoxTest.IntervalPrintDoesNotClobberRounding`, `ContractorCapdFullTest.GenerateTrace*`; the guard mechanism itself — tripwire abort + `ExpectClobber` heal, both regimes — is unit-tested directly in `util/test/rounding_test.cc`, including `EXPECT_DEATH` cases for the abort). The two regimes have **conflicting** ambient-mode expectations:

- **gaol** (IBEX's interval backend) is sound only under **`FE_UPWARD`**. Under any other mode its directed rounding inverts (`lo > hi`), so an inexactly-FP-representable subexpression collapses to an *empty* interval.
- **CAPD** (ODE backend) expects **`FE_TONEAREST`**; its `DoubleRounding` sets directed modes per-op but — as the dtor clobber tripwire revealed — **leaves the FPU in a directed mode (`FE_UPWARD`) on return rather than restoring nearest** (`make_capd_ode_cache`'s `IMap` build and the `run_capd_*` integrators both do this). So every CAPD adapter brackets its work in an `ExpectClobber` `NearestRoundingScope` that re-establishes nearest on exit, containing the clobber so callers (and their enclosing scopes' tripwires) see nearest. Crucially, **linking CAPD leaves the process FPU in `FE_TONEAREST`** (its static init / `roundNearest()`), so there is no longer a safe ambient mode to assume.

Invariant: **every mode-sensitive operation must establish its regime explicitly via the matching Scope — never rely on the ambient FPU state.** gaol/interval code needs `FE_UPWARD` (`UpwardRoundingScope` → `UpwardRounding` token); decimal formatting / libm / CAPD / NLopt need `FE_TONEAREST` (`NearestRoundingScope` → `NearestRounding` token).

**The two symmetric regimes** (`src/dreal/util/rounding.h`):

| | Interval regime | Nearest regime |
|---|---|---|
| Mode | `FE_UPWARD` | `FE_TONEAREST` |
| Scope (establish + mint) | `UpwardRoundingScope` | `NearestRoundingScope` |
| Token (compile-time proof) | `UpwardRounding` | `NearestRounding` |
| Token-gated consumers | `safe_mid`/`safe_diam`, `sub_*`/`add_*`, `make_sound_interval`, `ibex_hc4_backward` (`util/rounded_interval.h`) | `format_double` (`util/rounded_format.h`), `dump_json` (`util/json_guarded.h`); `run_capd_*` |
| Debug backstop | `DREAL_ASSERT_ROUNDING(FE_UPWARD)` | `DREAL_ASSERT_ROUNDING(FE_TONEAREST)` |

**Phase-hoisting.** `FE_UPWARD` is *not* established per-`Prune` — that paid an `fesetround` (pipeline-serializing) on every contractor invocation in the ICP hot loop. It is established **once per ICP phase** by an `UpwardRoundingScope` at each phase entry — `IcpSeq::CheckSat` (before the loop) and each `IcpParallel` worker (FPU mode is thread-local, so every worker mints its own). The `UpwardRounding` token is threaded as a parameter through `Contractor::Prune(ContractorStatus*, const UpwardRounding&)` and every override/combinator, AND through the eval/branch path (`safe_mid`/`safe_diam`, the `FormulaEvaluator`/`ExpressionEvaluator` `operator()`, `EvaluateBox`, the `Brancher`/`TerminationCondition` `std::function`s, `CounterexampleRefiner::Refine`). The token is **the sole proof** a gaol op may run: there is no minter but a scope, so the invariant is compile-time-enforced and direct callers (e.g. tests) must establish a scope. The pure-gaol leaves (`ContractorIbexFwdbwd::Prune`, `ContractorIbexPolytope::Prune`) and `ExpressionEvaluator::operator()` no longer self-guard; they `DREAL_ASSERT_ROUNDING(FE_UPWARD)` to verify the inherited phase mode in Debug. CAPD code establishes a nested `NearestRoundingScope` (`contractor_ode_lohner::Prune` / `generate_trace` / the CAPD cache build) and passes its token to `run_capd_*`; because CAPD calls *ibex* invariant sub-contractors, it re-establishes a further nested `UpwardRoundingScope` to mint the token they require. The `run_capd_*` / `make_capd_ode_cache` adapters themselves open an `ExpectClobber` `NearestRoundingScope` to contain CAPD's directed-mode clobber, so those outer `NearestRoundingScope`s see nearest at their dtor and their clobber tripwires pass. Standalone leaves self-establish their own scope: `Box::MaxDiam` mints an `UpwardRoundingScope`; the NLopt callbacks mint a `NearestRoundingScope` (no token — they call no token-gated consumer). `math.cc`'s `is_integer` / `convert_int64_to_double` are mode-**independent** (`modf` is an exact integer/fraction split; the range tests and `== 0.0` are exact; int→double within ±2^53 is exact) and therefore establish **no** scope at all — critically, `is_integer` is called per `pow` in the hot `ExpressionEvaluator::VisitPow` loop (which runs under `FE_UPWARD`), so a scope there would force a needless `FE_UPWARD`↔`FE_TONEAREST` flip on every call. The output printers (`prefix_printer`, the drivers' model/result output) route every scalar `double` through `format_double` and every json through `dump_json`, both token-gated — a SAT-model value formatted under `FE_UPWARD` would print the *wrong number*.

The sneaky part: a wrong ambient mode does **not** crash or warn — it silently produces an inverted interval that empties the box, surfacing as a **false `unsat`** (delta-complete soundness is violated) only on formulas containing inexact constant arithmetic inline in inequalities (e.g. `(* 3.3 x)`, `(* C (pow 10 -N))`). It is invisible on exactly-representable values (`0.5`, `0.25`, `*1.0`). Regression test: `test/dreal/api/test/gaol_directed_rounding_false_unsat_test.cc`; the nearest-regime analog is `test/dreal/util/test/rounded_format_test.cc`. Don't add floating-point/interval/printing code without an explicit `UpwardRoundingScope`/`NearestRoundingScope` for the regime it touches.

**Debug-only rounding assertions and the Debug gate.** `DREAL_ASSERT_ROUNDING(mode)` compiles to *nothing* under `NDEBUG` (it no longer even evaluates `fegetround()`), so it is free in release and safe to sprinkle densely at gaol/CAPD boundaries — it currently guards the entries of `ContractorIbexFwdbwd::Prune`, `ContractorIbexPolytope::Prune`, `Box::MaxDiam`, and the CAPD `contractor_ode_lohner::Prune`. `RoundingModeGuard` also maintains a debug-only `thread_local` shadow (`rounding_detail::g_rounding_shadow`) of the mode the guard stack expects; `DREAL_ASSERT_ROUNDING_CONSISTENT()` (placed at the gaol→CAPD boundary) asserts the live FPU register still matches that shadow, catching any `fesetround`/CAPD clobber that bypasses a guard — drift a plain guard cannot detect. Distinct from these Debug-only checks, the `RoundingModeGuard` **dtor clobber tripwire** (did the established mode survive to scope exit?) runs in *every* build and `abort()`s on violation; it is what caught CAPD's unrestored directed-mode clobber, and `ExpectClobber` scopes opt out of it where a clobber is expected and healed (see the FPU-rounding section above). Because the shadow checks only run in a **Debug** build, `./rounding_debug_gate.sh` builds the Debug test target (`cmake-build-debug`) and runs the suite, failing on any rounding-assertion abort (the known-flaky `EXPECT` trio does not fail it). Run it in CI / before merges. **`.diam()`/`.mid()` are NOT mode-safe getters** — they run gaol directed-rounding FP, sound only under FE_UPWARD; only `.lb()`/`.ub()` are pure stored-value reads. Solve-time call sites go through `safe_diam()`/`safe_mid()` (`util/rounded_interval.h`), which take the `UpwardRounding` token, `DREAL_ASSERT_ROUNDING(FE_UPWARD)`, and let the rounding lint forbid raw `.diam()`/`.mid()` elsewhere. (Legitimate exceptions, allow-listed: the SMT2 parser at parse time and `ibex`'s own interval `operator<<` — the latter is a mode *clobberer* (see the FPU-rounding section), so its two call sites bracket it in an `ExpectClobber` `NearestRoundingScope` rather than relying on the ambient FE_TONEAREST.)

**Typed directed-rounding doubles** (`util/rounded_interval.h`): the scalar analog of the `UpwardRounding` token. Hand-written `double` arithmetic feeding an interval endpoint is the trap — `ibex::Interval(mid - half, mid + half)` is mis-rounded under *every* single mode (the lower endpoint is pulled inward → too-narrow box). `Exact`/`RoundedDown`/`RoundedUp` encode the rounding direction in the type; `make_sound_interval(RoundedDown lo, RoundedUp hi)` only accepts an outward-rounded pair, so the compiler checks it. `sub_down`/`add_up`/etc. compute the round-down direction via `roundDown(x) = -roundUp(-x)` under FE_UPWARD (no per-op `fesetround`) and require the `UpwardRounding` token. `Tighten` (`context_impl.cc`) uses this for its delta-box construction; `test/dreal/util/test/rounded_double_test.cc` verifies the directed rounding matches gaol's own enclosure.

**Routing lint (`rounding_lint.py`) — there is no real clang-tidy.** This is a dependency-free regex check (the syntactic intent of a clang-tidy custom check, realized without the LLVM/libTooling build; no `.clang-tidy`, no `CMAKE_CXX_CLANG_TIDY`, nothing in CI). Run it with `python3 rounding_lint.py` (static, no build) or `./rounding_debug_gate.sh` (lint + Debug ctest gate). It forbids, in `src/dreal/`: raw `ibex::Function::backward` (route through `ibex_hc4_backward`), raw `.mid()`/`.diam()` (route through `safe_mid`/`safe_diam`) — all in `util/rounded_interval.h` — intervals built from hand-written scalar `+`/`-` arithmetic (use `make_sound_interval`), and raw json `.dump(` (route through `dump_json`, `util/json_guarded.h`). Scalar `double`→decimal routing (`format_double`) is **not** regex-detectable, so it is enforced at *compile time* by `format_double` requiring the `NearestRounding` token, not by the lint. Legitimate exceptions carry an inline `// rounding-lint: allow <reason>` marker; the parser/scanner sources and the wrapper-definition files (`rounded_interval.h`, `rounded_format.h`, `json_guarded.h`) are skipped.

**`filter_assertion` soundness**: There was a soundness bug where strict upper bounds were handled incorrectly due to a wrong `nextafter()` call. The `forward`/`backward` naming in `substitutions_map` also had a soundness bug that was fixed. Be careful around strict vs. non-strict inequality handling in contractors and the SAT interval logic.

**ODE performance baseline**: Pre-Codac-elimination benchmarking confirmed CAPD order-20 was at or below Codac CtcLohner runtime on all tested ODE benchmarks (cardiac, prostate, bouncing-ball families). See `CODAC_MIGRATION.md` for the historical benchmark tables. The `--capd-t-gate` / `--capd-ndim-gate` CLI flags have been removed — they were only meaningful under the old Codac/CAPD gated hybrid.

**Benchmarking instrumentation**: Several `std::cerr` prints and JSON dumps exist specifically for benchmarking runs. Log levels (TRACE/DEBUG/INFO) are tuned so that `--verbose` (DEBUG) is useful for development without flooding output on large queries. TRACE is for deep debugging only.
