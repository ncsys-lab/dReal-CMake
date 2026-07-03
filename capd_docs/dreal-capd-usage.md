# What dReal currently uses from CAPD (the audit baseline)

Grounded in `src/dreal/contractor/odes/contractor_odes_capd.cc` and
`contractor_odes.cc` as of branch `rounding-mode-fixes` (2026-06). This is the
reference point for `AUDIT.md`: every "opportunity" is a CAPD capability **not**
on this list, or one used only partially.

## Symbols actually referenced

`capd::IMap`, `capd::IOdeSolver`, `capd::ITimeMap`, `capd::interval`,
`capd::IVector`, `capd::IntervalError`, and the three C0 set types
`capd::C0Rect2Set` / `capd::C0HORect2Set` / `capd::C0TripletonSet`. Build flag
`CAPD_INTERVAL_TYPE=NATIVE` (double-based intervals).

## The integration path (per ODE constraint, per ICP call)

1. **RHS → `capd::IMap`** (`CapdOdeCache`): the vector-field string is parsed and
   its automatic-differentiation tree built at `IMap` construction — the
   expensive setup, done **once per flow** and cached. `fn_fwd` = f(x), `fn_bwd`
   = −f(x) for backward integration. Non-integrated flow vars are CAPD
   *parameters*, bound per-call.
2. **`capd::IOdeSolver solver(map, order)`** — order-N Taylor stepper. Order from
   `--ode-taylor-order` (fwd, default 12) / `--ode-backward-order` (bwd).
   `configure_capd_solver` sets `setAbsoluteTolerance`, `setRelativeTolerance`
   (both 1e-10), and `setMaxStep` only when `--ode-max-step > 0` (else fully
   adaptive step control — CAPD's default).
3. **`capd::ITimeMap time_map(solver); time_map.stopAfterStep(true)`** — walk the
   adaptive steps one at a time to `t_ub`, using `completed()` and
   `getCurrentTime()`. The C0 set type (`SetT`) is chosen by `--ode-c0-set`.
4. **Per step:** `solver.getCurve()` + `solver.getStep()`; the curve is evaluated
   over time sub-intervals with `curve(sub)` (`operator()`) and, since the
   2026-06 fix, `curve.timeDerivative(sub)` for the centered-in-time range
   (`HULL_COMPLETENESS.md`). Sub-slices form the tube `state` + window-clipped
   `gate_state`.
5. **Downstream (`contractor_odes.cc`):** the slice intervals are written into the
   box and the existing **ibex** HC4 contractors do the `forall_t` invariant
   refutation and terminal-gate narrowing. CAPD only produces enclosures; the
   contraction logic is ibex's.
6. **Exceptions:** any CAPD throw (step-control divergence, mid-enclosure
   singularity) → catch-all → skip narrowing (sound; an un-narrowed box is never
   a false `unsat`).

## Conspicuously NOT used (audit territory — confirm each in the docs)

- **C1/C2/Cn solvers & variational equations** — derivatives of the flow w.r.t.
  initial conditions (the monodromy/Jacobian). Could sharpen X₀ (backward)
  narrowing and interval-Newton steps. dReal uses only C0 (value-only) sets.
- **`PoincareMap` / sections** — section-crossing maps; a pinned terminal time is
  a degenerate time-section, so this *might* fit terminal-gating more naturally
  than tube-slicing.
- **`DiffInclusion`** — rigorous enclosures for ODEs with bounded parameter
  uncertainty; dReal currently treats uncertain flow params as CAPD parameters
  over a box.
- **Set reorganization / higher-order & affine sets** beyond the three C0 types
  wired to the flag — the lever against the wrapping effect.
- **Curve methods beyond `operator()`/`timeDerivative`** — `derivative`,
  `hessian`, `jet`, `eval`, affine/centered forms.
- **Threading** (`group__threading`) — CAPD-internal parallelism (dReal's
  parallelism is ICP-level, one solver per worker).
- **Step-control strategy choices** (`StepControl`, `MpStepControl`) and
  multiprecision interval types.

> Source-fidelity: the "NOT used" bullets are hypotheses to confirm against the
> CAPD docs/headers, not asserted capabilities. Mark anything unverified.
