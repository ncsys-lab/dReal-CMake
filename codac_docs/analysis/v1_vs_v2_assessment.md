# Should we backtrack from Codac v2 to v1?

**TL;DR — no.** The v1 manual page lists more dynamic contractors than v2 ships, but for our use case (forward/backward narrowing of `ẋ = f(x)` between endpoint intervals) the only v1-exclusive ODE contractor is `CtcPicard`, and v1's own docs say Picard is *worse* than Lohner once tubes are thin. Both versions implement the **same order-2 Lohner**, so backtracking does not close the ~5× CAPD performance gap on `bouncing_ball_with_drag_10_0`. Rewriting our integration site for the v1 API costs ~200–300 lines of mechanical translation plus a build-system pivot, in exchange for one extra contractor with unclear benefit.

---

## What v1 has that v2 doesn't

From the v1 manual catalog at `https://codac.io/v1/manual/05-dynamic-contractors/`:

| Contractor | Constraint | In v2 install? | Useful for dReal's ODE workload? |
|---|---|---|---|
| `CtcLohner` | ẋ = f(x), Lohner method | yes | (already using) |
| `CtcPicard` | ẋ = f(x), Picard iteration | **no** | maybe — see "Why Picard isn't a win" |
| `CtcDeriv` | ẋ = v (tube + derivative tube) | yes | no — needs explicit `v` tube |
| `CtcEval` | y = x(t) | yes | no — we already pin endpoint gates |
| `CtcDelay` | x(t) = y(t+a) | no | no — we don't have time-delay constraints |
| `CtcLinobs` | ẋ = Ax + Bu | no | no — our ODEs are nonlinear |
| `CtcChain` | ẋ₁ = x₂, ẋ₂ = x₃, … | no | no — our ODE encoding isn't in chain form |

The only v1-only contractor that intersects our use case is `CtcPicard`.

## Why Picard isn't a win

1. **v1 docs themselves rank Lohner above Picard for thin tubes.** From the v1 CtcLohner page:

   > "This contractor is supposed to yield better results than [Picard] as long as the tubes are 'thin enough'."

   ICP narrows tubes aggressively across iterations. By the time the contractor matters in our solver, tubes are thin. Picard would be the fallback choice, not the upgrade.

2. **Same order ceiling.** Picard iteration converges to the trajectory tube but each *iteration* uses a Picard step (essentially first-order operator with inflation). It is not a higher-order Taylor method. It does not buy us what CAPD's order-20 Taylor was buying us.

3. **Data-dependent iteration count.** Picard's convergence test is `x_enclosure.is_interior_subset(x_guess)`. For non-contractive slices it does not converge at all — the contractor either aborts or loops near the contractivity threshold. Predictable per-call cost is one of the things we *gained* moving away from CAPD's adaptive step adjustment; Picard would re-introduce variance.

4. **One extra knob, no obvious better defaults.** `delta = 1.1` (default) controls inflation. Smaller `delta` is tighter but may not converge; larger is wider but converges faster. No public guidance for which to pick on stiff/oscillatory dynamics like bouncing-ball-with-drag.

## v1's CtcLohner is the same algorithm as v2's

Verified by reading both source files (`codac_CtcLohner.cpp` on `codac1` and `codac2_CtcLohner.h`/`.cpp` in our install):

- Both define `IntervalVector z; //!< Taylor-Lagrange remainder (order 2)`
- Both compute `z = 0.5 * h * h * f.jacobian(u) * f.eval_vector(u)` — the order-2 Taylor term `h²/2!`
- Both expose only `contractions` and `eps` as constructor knobs
- Both throw the same `GlobalEnclosureError` when the local step blows up

Replacing v2 `CtcLohner` with v1 `CtcLohner` would change zero numerics. The ~5× gap to CAPD remains.

## What backtracking costs

### Code

`contractor_odes_codac.cc` rewrite (currently 388 lines, C++20):

| Site | v2 (current) | v1 |
|---|---|---|
| Function type | `codac2::AnalyticFunction<VectorType>` built from `dReal::Expression` via `translate_expr` | `ibex::Function` — we already build these via `IbexConverter` for non-ODE contractors |
| Tube type | `codac2::SlicedTube<IntervalVector>` | `codac::TubeVector` |
| Tube construction | `create_tdomain` + `SlicedTube` constructor | `TubeVector(t_domain, dt, n)` constructor |
| Endpoint reads | `tube.first_slice()->codomain()`, `tube.last_slice()->codomain()` | `tube(t0)`, `tube(t_ub)` |
| Trace path | `codac2::LohnerAlgorithm` (public) | Not exposed — would need to replicate via `CtcLohner` + small step tubes or via `CtcPicard.guess_kth_slices_envelope` |
| C++ standard | C++20 (concepts, std::numbers required by Codac headers) | C++17 (matches the rest of dReal) |

Estimate: ~200–300 lines rewritten in `contractor_odes_codac.{h,cc}`. dReal-side contractor (`contractor_odes.cc`) shape doesn't change because the Prune signature stays the same.

The `--visualize` / `generate_trace` path is the one part that doesn't translate cleanly — v1 doesn't expose a public step-by-step API equivalent to v2's `LohnerAlgorithm::integrate(1); getLocalEnclosure()` loop.

### Build system

CMakeLists currently downloads pre-built Codac v2 archives keyed on (macOS Sonoma/Sequoia/Tahoe, ARM64/x86_64). For v1, we'd either:
- (a) Switch to `ExternalProject_Add(codac_external GIT_TAG v1.5.7)` and build from source. This was the approach in `CODAC_MIGRATION.md` Phase 1 before the project moved to pre-built archives. Adds ~5 min to the first build.
- (b) Find or build our own v1 binary archive. lebarsfa's GitHub Releases for codac1 include Ubuntu/Debian arm64 binaries but not macOS arm64.

(a) is the realistic path; (b) is a maintenance overhead we shouldn't take on.

### Maintenance trajectory

- v1 tagged releases: 10 between April 2024 and March 2024. Last tag: `v1.5.7` (March 18, 2024).
- v2 tagged releases: 50+ between April 2024 and June 2026. Latest: `v2.0.3` (June 4, 2026).
- `codac1` branch commits in 2026 are all "Update workflows" / dependency-bump-style PRs from lebarsfa.

Reading this: v1 is in maintenance mode (no new features, but the build is kept current). Moving back means relying on a branch the core maintainers have de-prioritized. If we hit a v1-specific bug — e.g. an interaction between v1's `Tube` and our IBEX fork at a future IBEX upgrade — we'd be on our own to patch it.

## Downsides summary

1. **Performance**: same Lohner algorithm = no win on the bouncing-ball-class gap.
2. **Picard ranks below Lohner per v1's own docs** for thin tubes (our common case).
3. **Rewrite cost**: ~200–300 LoC in `contractor_odes_codac.cc` plus a tube API change.
4. **Visualization regression risk**: v1 doesn't expose the same step-by-step API as v2's `LohnerAlgorithm` for `--visualize`.
5. **Build-system pivot**: pre-built archives → source-built ExternalProject for Codac.
6. **Maintenance trajectory**: v1 is feature-frozen; v2 gets all new development.
7. **Two-stage migration risk**: we already migrated CAPD → v2; migrating v2 → v1 is a second move that wasn't part of the original Codac plan.
8. **macOS ARM64 not explicitly tested** for v1 — the macOS install page returned 404 at fetch time; we'd be relying on source-build defaults.

## Where the actual performance gap is

The 5× gap on `bouncing_ball_with_drag_10_0` is the **Taylor order-2 vs order-20** gap. Neither v1 nor v2 closes it because both use the *same* order-2 Lohner.

Real options to close it (none are "go to v1"):

1. **Restore CAPD via the documented ARM64 FILIB-bypass patch.** `CODAC_MIGRATION.md` § "The Correct ARM64 Fix" has a two-file patch deliverable via `ExternalProject_Add PATCH_COMMAND`. This is the only path that restores order-20 Taylor. Cost: platform-specific build complexity + reconstructing `contractor_odes_capd.cc` from session transcript.
2. **Use `LohnerAlgorithm` directly for the BWD pass.** v2 exposes `LohnerAlgorithm` with `forward=false`, which lets us do proper backward integration instead of the current "skip CtcLohner on BWD because the constructor swap is unsound" workaround. This narrows X_0 on benchmarks where backward propagation provides complementary information. Doesn't change the order-2 ceiling on bouncing-ball, but is the one Codac-native untapped opportunity. See `alternatives_to_ctclohner.md`.
3. **Write a higher-order custom Taylor integrator on top of IBEX intervals.** Substantial new code (the actual algorithm CAPD implements). Not justified for CAV26-focused research.

## Recommendation

Stay on v2. The v1 manual's larger contractor list reflects v1's broader product surface (delay systems, linear systems, chain rule), not better ODE coverage for our problem. Pursue option (2) above as the realistic Codac-native experiment, and keep CAPD restoration on the table as the only fix for the order-2 ceiling — separable from this question.
