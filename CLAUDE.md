# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

dReal4 is a delta-complete SMT solver for nonlinear arithmetic over the reals. It takes SMT-LIB2 (`.smt2`) or Delta-Real (`.dr`) formatted formulas and checks satisfiability up to a precision parameter delta. The `cav26` branch holds pattern-matching/lemma-reuse research; `upgrade-ibex` replaces the ODE integration backend with Codac v2.

## Build

**Prerequisites** (macOS — ARM or x86 Homebrew; Rosetta no longer required):
- bison, flex, gmp, cadical, eigen (install via `/opt/homebrew/bin/brew` on Apple Silicon)
- CMake fetches and builds IBEX (`lebarsfa/ibex-lib`) and Codac (`codac-team/codac`) automatically

**Full build** (first time — creates `gcc_build/`):
```bash
./FULL_BUILD.sh
```

**Incremental build** (subsequent builds):
```bash
./BUILD.sh
```

Both scripts build target `dreal4` with `-j8`. The binary is at `gcc_build/dreal4`.

**macOS note**: The Codac migration (see `CODAC_MIGRATION.md`) removed the x86/Rosetta requirement. CMakeLists.txt now detects ARM or x86 Homebrew automatically. The old `rosetta_cmake.sh`/`rosetta_lldb.sh` wrappers are still present but no longer needed for builds that don't use the old CAPD/FILIB dependencies.

**Docker** (avoids local dependency setup):
```bash
docker build --platform linux/amd64 -t dreal/my_dreal_image:1.0 -f Dockerfile.dreal_ubuntu .
cat query.smt2 | docker run --platform linux/amd64 --rm -i dreal/my_dreal_image:1.0 ./dreal4 --in --model
```
Note: Docker on macOS may have `-j` filesystem bugs — reduce to `-j1` if you see bad file descriptor errors.

## Running Tests

Tests are built as `dreal4_cmake_test`. Build and run:
```bash
cd gcc_build
cmake --build . --target dreal4_cmake_test -j8
ctest                                      # run all tests
./dreal4_cmake_test --gtest_filter="*Box*" # run a single test by name pattern
```

Test sources live under `test/dreal/` mirroring `src/dreal/` structure (e.g., `test/dreal/util/test/box_test.cc`).

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
   - `contractor_ode_lohner`: ODE contractors (Codac/IBEX-based, replaces old CAPD contractor)

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
- **IBEX** (`lebarsfa/ibex-lib@ibex-2.8.9.1`): Built via ExternalProject into `gcc_build/ibex-install/`
- **Codac** (`codac-team/codac@v2.0.2`, `WITH_CAPD=OFF`): Built via ExternalProject into `gcc_build/codac-install/`
- **fmt**, **spdlog**, **nlopt**: Via FetchContent
- **GTest**: Via FetchContent

The old `ncsys-lab/ibex-lib`, `ncsys-lab/capdDynSys-4.0`, and FILIB have been replaced.
See `CODAC_MIGRATION.md` for the full migration plan and current status.

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

**`upgrade-ibex`** (current): Replaces the ODE integration backend on top of `tacas26-odes`:
- Migrates from `ncsys-lab/capdDynSys-4.0` + `ncsys-lab/ibex-lib` to Codac v2 + `lebarsfa/ibex-lib`.
- `contractor_odes_codac.cc`: `CtcLohner` with `TimePropag::FWD_BWD` (5 contractions, 50 steps) for integration; `LohnerAlgorithm` for trace/visualization.
- Known performance gap: ~13 s vs ~0.5 s (CAPD order-20) on `bouncing_ball_with_drag_10_0.smt2`; caused by Codac's fixed Taylor order-2. See `CODAC_MIGRATION.md` for the CAPD v6 ARM64 fix if this needs to change.
- CAPD v6 direct integration was attempted and abandoned (FILIB x86-only blocker); details in `CODAC_MIGRATION.md`.

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

**Thresholds**: regression if wall time >1.5× baseline or result changes; exceptional if <0.6× baseline. Correctness flips (SAT↔UNSAT) are always escalated immediately regardless of timing.

**Benchmark sources** (raw `.smt2` files, not in this repo):
- `~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/` — github_oct5_ family
- `~/Documents/new_dreal/nraode_to_nra/VNAMSCwI_satoct11/rolled/` — tacas_c2e2_ family
- `~/Documents/new_dreal/nraode_to_nra/SARADC_tueoct14/` and `REB_SAR_k1_dec9/` — 1mhz_ family

**Manual invocation** (if not using the skill):
```bash
python3 benchmark/select.py | bash benchmark/run_batch.sh benchmark/results/run_$(git rev-parse --short HEAD)_$(date +%s)
python3 benchmark/parse_results.py <results_dir>
python3 benchmark/aggregate.py <results_dir>
```

## Key Design Notes

**dReal3 backward compatibility is intentional.** The DR parser (`src/dreal/dr/`) handles the older dReal3 ODE syntax. Don't break this.

**`auditor.cc`** (`src/dreal/solver/auditor.cc`) is a development/verification tool. It reprints learned lemmas in dReal3-compatible format so they can be re-checked by dReal3 independently. It is not part of the core solving loop.

**FPU rounding mode**: The solver sets the FPU rounding mode explicitly (fesetround). There are guards in `prefix_printer.cc` and `rounding mode guards` in several places. Interval arithmetic requires directed rounding; don't add floating-point code without considering this.

**`filter_assertion` soundness**: There was a soundness bug where strict upper bounds were handled incorrectly due to a wrong `nextafter()` call. The `forward`/`backward` naming in `substitutions_map` also had a soundness bug that was fixed. Be careful around strict vs. non-strict inequality handling in contractors and the SAT interval logic.

**ODE performance baseline** (`upgrade-ibex` branch, ARM64 macOS): `bouncing_ball_with_drag_10_0.smt2` (10 modes) takes ~13 s with Codac `CtcLohner` (Taylor order 2). Old CAPD (Taylor order 20, x86 Rosetta) took ~0.5 s. The 26× gap is accepted because CAV26 research focus is pattern-matching/lemma reuse, not ODE integration speed. See `CODAC_MIGRATION.md` for the CAPD v6 ARM64 fix if ODE speed becomes critical.

**Benchmarking instrumentation**: Several `std::cerr` prints and JSON dumps exist specifically for benchmarking runs. Log levels (TRACE/DEBUG/INFO) are tuned so that `--verbose` (DEBUG) is useful for development without flooding output on large queries. TRACE is for deep debugging only.
