# Realistic alternatives to v2 CtcLohner

A focused look at what other ODE-integration paths are available to dReal right now, given what's actually buildable on the current toolchain.

## What we have today

`contractor_odes_codac.cc` calls `codac2::CtcLohner::contract(tube, TimePropag::FWD_BWD)` for the FWD contractor, and skips ODE integration entirely for the BWD contractor (because the constructor's `m_vars_0 ↔ m_vars_t` swap asks the wrong question for non-time-symmetric dynamics — see `CODAC_MIGRATION.md` § "Correctness analysis of the BWD swap").

## Options ranked

| # | Approach | Codac-native? | Taylor order ceiling | Effort | Expected gain on bouncing-ball | Verdict |
|---|---|---|---|---|---|---|
| 1 | `LohnerAlgorithm` direct for BWD pass | yes | order 2 (unchanged) | ~150 LoC, localized to `contractor_odes_codac.{h,cc}` and one line in `contractor_odes.cc` | unlikely to move bouncing-ball; could narrow X_0 on benchmarks where backward propagation helps | **best Codac-native experiment** |
| 2 | Two separate CtcLohner calls (FWD-only, then BWD-only) instead of `FWD_BWD` | yes | order 2 (unchanged) | 1-line | likely zero; worth as A/B baseline | cheap baseline |
| 3 | Switch v2 CtcLohner → v1 CtcLohner | yes | order 2 (unchanged) | ~200–300 LoC + build-system pivot | zero (same algorithm) | not justified — see `v1_vs_v2_assessment.md` |
| 4 | Add v1 CtcPicard as a second contractor | yes (after #3) | not Taylor at all; iterative | (#3) + ~150 LoC | unlikely to win on thin tubes per v1 docs; might help in early ICP | not justified |
| 5 | Restore CAPD via ARM64 FILIB-bypass patch | no — restores CAPD dep | **order 20** | platform patch + reconstruct `contractor_odes_capd.cc` from migration transcript | ~5× (matches old CAPD measurements) | **the actual fix for the order-2 ceiling**, separate scope |
| 6 | Write higher-order custom Taylor integrator | no — new code | configurable | weeks | matches (5) in principle | not justified for CAV26 |

## Why #1 is the best Codac-native experiment

`codac2::LohnerAlgorithm` exposes `forward = true | false` directly. Our `run_lohner_trace` already constructs one with `forward=true`. A `run_lohner_backward` that constructs one with `forward=false` and iterates `integrate(1)` from `Xt` down to `t=0` gives us a sound backward image that the current `CtcLohner` swap cannot.

Sketch:

```cpp
CodacOdeResult run_lohner_backward(
    const std::shared_ptr<CodacOdeCache>& cache,
    const std::vector<std::pair<double,double>>& u0_bounds,
    const std::vector<std::pair<double,double>>& X_t_bounds,
    double t_ub,
    int n_steps_hint)
{
    // Build Xt from X_t_bounds.
    // Build X0 from u0_bounds.
    // const int n_steps = clamp(max(n_steps_hint, ceil(t_ub*2.0)), n_steps_hint, 60);
    // const double h = t_ub / n_steps;
    // codac2::LohnerAlgorithm algo(&cache->fn, h, /*forward=*/false, Xt, contractions, eps);
    // Walk n_steps backward: algo.integrate(1); algo.contractStep(...); collect.
    // Intersect final enclosure with X0; return narrowed (X0, Xt).
}
```

Wire-up in `contractor_odes.cc` line 246: replace the current "BWD Step 4 skip" with a call to `run_lohner_backward` and intersect its result into the box.

### What this does *not* fix

Bouncing-ball perf. The order-2 ceiling is the same. Where this helps is on benchmarks where the FWD `FWD_BWD` pass is leaving `X_0` narrowing on the table — likely the long-horizon cardiac family.

## Why #2 is a useful cheap baseline first

Splitting `FWD_BWD` into a `FWD` call then a `BWD` call on the same tube might just reproduce `FWD_BWD` semantics — but if the interleaving with other contractors in the fixpoint loop matters, it could either improve narrowing per pass or expose extra room for ICP to bisect. One-line change to validate before committing to #1.

## Operational plan

1. Save documentation snapshot (this `codac_docs/` directory) so future sessions don't re-fetch. **Done.**
2. Implement #2 (one-line change). Run `/benchmark` smoke. Decide.
3. If #2 doesn't move the needle (expected), implement #1. Run `/benchmark-baseline` then `/benchmark` + targeted re-runs on hard ODE benchmarks.
4. Correctness gate: any SAT↔UNSAT flip vs baseline aborts the experiment (the BWD-skip was specifically introduced for `prostate_h2` and similar).
5. Record outcome in `CODAC_MIGRATION.md` § "Open lines of attack" alongside the existing entries.
6. Keep CAPD restoration on the table as the separate-scope path to closing the bouncing-ball gap.
