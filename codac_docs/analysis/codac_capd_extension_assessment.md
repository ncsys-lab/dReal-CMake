# Should we use the `codac-capd` extension?

> **Outcome (2026-06-06).** We took the **raw direct CAPD** path (not this extension). Two reasons consolidated the decision: (1) CAPD master added a clean `CAPD_INTERVAL_TYPE=NATIVE` option in the v6.1.0 dev cycle, so the FILIB ARM64 blocker disappeared without needing the documented patch, and (2) raw CAPD lets us keep the pre-built Codac archives. See `CODAC_MIGRATION.md` § "CAPD-Lohner gated hybrid" for the landed implementation; the analysis below is preserved for historical context.

**TL;DR — it's a cleaner glue layer, not a solution.** The extension converts types between CAPD and Codac (Interval, IntervalVector, IntervalMatrix, SolutionCurve → SlicedTube) but offers no `CtcCapd` contractor and does not bundle, build, or otherwise insulate us from CAPD itself. The ARM64 FILIB blocker that killed our previous direct-CAPD attempt applies identically here, and on top of that we'd have to switch from pre-built Codac archives to a source-built Codac (so it can be compiled with `WITH_CAPD=ON`).

Full reasoning in `codac_docs/v2/capd_extension.md`. The remaining decision is whether to take on the CAPD restoration *at all*; if yes, the extension is the better of two implementation paths.

## Comparison: codac-capd extension vs raw CAPD (the originally-abandoned approach)

| Step | Raw CAPD (abandoned attempt) | codac-capd extension |
|---|---|---|
| Get CAPD building on ARM64 | FILIB-bypass patch (documented) | **same** FILIB-bypass patch |
| Add CAPD as a dReal dependency | `ExternalProject_Add(capd_external)` | `ExternalProject_Add(capd_external)` |
| Get Codac talking to CAPD | direct includes | rebuild Codac v2 from source with `WITH_CAPD=ON` (lose pre-built archive convenience) |
| Build the IMap from dReal Expression | hand-rolled `IMap` string builder | **same** hand-rolled `IMap` string builder |
| Set up integrator with order 20 | `capd::IOdeSolver(map, 20)` | **same** `capd::IOdeSolver(map, 20)` |
| Run forward integration | `capd::ITimeMap` | **same** `capd::ITimeMap` |
| Convert result to internal tube | hand-rolled (30–50 LoC) | `to_codac(SolutionCurveWrapper, tdomain)` (single call) |
| Forward-backward propagation | DIY (or run a second `IOdeSolver` for `-f(x)`) | **same** DIY |
| Build complexity | one ExternalProject for CAPD | two coordinated ExternalProjects (CAPD then Codac-from-source) |

Net delta: extension saves ~30–50 LoC at the cost of one extra ExternalProject and the loss of the pre-built Codac archives our current build relies on.

## The actual unknowns

The codac-capd extension assessment only matters if we decide to pursue CAPD restoration. That's the bigger question:

1. **Is the bouncing-ball-class perf gap painful enough to warrant the work?** Per `CODAC_MIGRATION.md`: "The 26× gap is accepted for CAV26 work because the paper's contribution is pattern-matching/lemma reuse, not raw ODE integration speed." Has that calculus changed?

2. **Will the FILIB-bypass patch be robust?** The documented patch is two-file and proven to work on ARM64. Risk: future CAPD upstream changes may move the FATAL_ERROR or change the way intervals are wired.

3. **Maintenance burden going forward:** we'd be maintaining (a) a patched CAPD, (b) a from-source Codac, (c) the contractor_odes_capd.cc translation layer.

4. **Hybrid use as the user suggested** ("in addition to CtcLohner maybe?"): the contractor framework supports composition. A CAPD-based contractor could be added alongside `contractor_ode_lohner` and the fixpoint loop would interleave them. Where CAPD gives tighter enclosures, the box shrinks more per pass.

## Recommended decision tree

```
Q1: Is bouncing-ball / quad / crazyflie perf blocking research progress?
  ├── No  → stay on v2 CtcLohner.
  │        Pursue the LohnerAlgorithm-direct BWD experiment (see
  │        alternatives_to_ctclohner.md option #1).
  │        Total effort: ~150 LoC.
  └── Yes → restore CAPD. Of the two paths:
            ├── codac-capd extension: marginally cleaner code,
            │   but requires source-built Codac.  Pick this if
            │   we plan to use other codac2 features that aren't
            │   in the pre-built archives.
            └── Raw CAPD: more code, simpler build.  Pick this if
                we want to keep the pre-built Codac archives.
            (Either way: FILIB patch + ExternalProject(CAPD)
            + contractor_odes_capd.cc reimplementation.)
```

## What the user's "in addition to CtcLohner" idea unlocks

Running both contractors in the fixpoint isn't wasteful — they have complementary strengths:

- `CtcLohner` (order 2, fast): handles thin tubes well, narrows both endpoints jointly in one call
- `CtcCapd` (order 20, slower per call but tighter): closes long-horizon enclosures where order 2 widens out of usefulness

Composition pattern (in `theory_solver.cc` analogous to current ODE wiring):

```
ctcs.insert(nl_ctcs);
for (each ODE constraint) {
  ctcs.insert(ode_lohner_fwd);     // cheap first pass
  ctcs.insert(nl_ctcs);
  ctcs.insert(ode_capd_fwd);       // tight follow-up if lohner left slack
  ctcs.insert(nl_ctcs);
  // (same for bwd)
}
```

Cost concern: ICP calls Prune many times. If `CtcCapd` is ~10× slower per call than `CtcLohner`, doing both unconditionally costs us on the easy benchmarks where Lohner alone suffices. Mitigations:
- Run Lohner first; only run CAPD if the box hasn't converged after N Lohner-led fixpoint iterations
- Detect "Lohner-friendly" benchmarks (thin tubes early, short horizon) and skip CAPD
- A simple gate based on `t_ub > threshold` or `max(slice_diam) > threshold`

These are second-order tuning concerns. The first-order question is whether to commit to CAPD restoration at all.
