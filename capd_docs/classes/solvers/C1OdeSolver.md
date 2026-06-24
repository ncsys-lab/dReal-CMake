# Class: C1 / `C2OdeSolver` (= `capd::IC2OdeSolver`) — first/second-order variational

## What it is
Rigorous solvers that integrate the **variational equations** alongside the trajectory: the
flow value **and** its derivatives w.r.t. initial conditions (monodromy matrix; Hessian for
C2). There is no standalone `C1OdeSolver` class — `IOdeSolver` (`OdeSolver`) already does
**first**-order variational (`encloseC1Map`), and `IC2OdeSolver` = `C2OdeSolver<IMap>` adds
**second**-order. (Page found: `classcapd_1_1dynsys_1_1C2OdeSolver.html`.) Template:
`C2OdeSolver<MapT, StepControlPolicyT = ILastTermsStepControl, EnclosurePolicyT = HighOrderEnclosure, CurveT = C2Curve<…>>`.

## Key API (confirmed in `C2OdeSolver` page + `OdeSolver` page)
First-order (on the C0 `OdeSolver` itself):
- `encloseC1Map(t, x0, x, o_phi, o_rem, o_enc, o_jacPhi, o_jacRem, o_jacEnc)` — value + Jacobian
  enclosure + their remainders/enclosures over the whole step.
- `operator()(VectorType, MatrixType& o_resultDerivative)` (autonomous) and the `(t, v, …)`
  nonautonomous form — image plus ∂φ/∂x₀; a `(v, derivative, o_…)` form propagates a given
  initial derivative matrix.
- `ITimeMap::operator()(time, set, MatrixType& derivative)` returns the monodromy at `time`.

Second-order (`C2OdeSolver`):
- `encloseC2Map(t, x, xx, o_phi, o_rem, o_enc, o_jacPhi, o_jacRem, o_jacEnc, o_hessianPhi, o_hessianRem, o_hessianEnc)`.
- `operator()(VectorType, MatrixType&, HessianType&)` and `(v, V, H, MatrixType&, HessianType&)`
  — value + first + second derivatives; `computeRemainder(t, xx, C2TimeJetType&, C2TimeJetType&)`.
- `getCurve()` returns a `C2Curve` exposing `hessian(h)`.

Default step control for `IC2OdeSolver` is `ILastTermsStepControl` (header-confirmed).

## dReal status
**Unused.** dReal binds only `IOdeSolver` with C0 sets; `IC2OdeSolver`, the C1/C2 sets, and
all `encloseC1Map`/`encloseC2Map`/monodromy paths are not referenced anywhere in
`src/dreal/contractor/odes/` (grep-confirmed empty).

## Why it might matter — the headline backward-narrowing opportunity
dReal's backward narrowing of the **initial-condition box** is currently done by ibex HC4 on
C0 slice values, which sees no flow sensitivity. The monodromy matrix `M = ∂φ_t/∂x₀` (from
`encloseC1Map` / the `MatrixType& derivative` TimeMap overload) enables a rigorous
**mean-value / interval-Newton** contraction: given a tight terminal box, `x₀ ∈ x₀ − M⁻¹·(φ(x₀) − target)`
sharpens the initial set in a way axis-aligned HC4 cannot. This is the single most promising
unused CAPD capability for X₀/backward tightness (a **completeness** lever — sharper enclosures
never cause false `unsat`). Cost: switch the solver typedef to `IC2OdeSolver` (or use the C1
outputs already on `IOdeSolver`), seed a C1 set, add an interval-Newton step, manage the per-step
overhead (variational integration is dimension² heavier). Risk: moderate — new contraction logic
plus invertibility handling for `M`; soundness is preserved by construction (any failure → skip).

## Source
[C2OdeSolver.h](../../../../CAPD/docs/html/C2OdeSolver_8h.html) ·
[class page](../../../../CAPD/docs/html/classcapd_1_1dynsys_1_1C2OdeSolver.html)
