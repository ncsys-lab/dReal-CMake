# DiffAlgebra (group `diffAlgebra`)

**What it is.** CAPD's "differential algebra" layer: the data structures and evaluation
logic for a *parametric Taylor curve* `c(t, x_0)` produced by an ODE step, together with
its derivatives with respect to the initial point. Holds `Curve`, `BasicCurve`,
`C2Curve`/`CnCurve` (higher-order space-derivative variants), `SolutionCurve` (a chain of
per-step curves for long-time integration), `Jet`/`Hessian` (multivariate-polynomial
coefficient stores), and the `*TimeJet` coefficient containers.

**Key contents (relative to dReal).**
- `Curve<BaseCurveT,true>` — the interval-curve dReal evaluates via `solver.getCurve()`.
  Its `operator()` already builds the mean-value (centered-in-space) enclosure
  `phi + jacPhi·deltaX ∩ direct`; `timeDerivative` gives the time-derivative enclosure.
  See `../concepts/curves-and-jets.md` and `../classes/curves/Curve.md`.
- `SolutionCurve` — the long-time functional object returned by
  `timeMap(t, set, solution)`; supports the *same* eval API across multiple steps, and (for
  a C1 set) `derivative(t)` = the monodromy/flow-Jacobian at intermediate time.
- `BasicCurve` / `BasicC2Curve` / `BasicCnCurve` — raw coefficient storage (center / interval
  / remainder / matrix coefficients); the evaluation classes wrap these.
- `Jet`, `Hessian` — truncated-power-series coefficient containers used for jet propagation.

**dReal status.** Used (C0 path only): dReal evaluates `Curve::operator()` and
`Curve::timeDerivative`. The C1/C2/Cn curves, `SolutionCurve::derivative`, and `Jet`/`Hessian`
are unused. See `dreal-capd-usage.md`.

**Why it might matter.** This is where dReal's 2026-06 tube tightening came from
(`timeDerivative` mean-value form). The unused C1 monodromy data is the main remaining lever
for backward (X₀) narrowing.

**Source.** [group__diffAlgebra.html](../../../CAPD/docs/html/group__diffAlgebra.html)
