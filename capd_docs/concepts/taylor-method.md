# Concept: One-step Taylor method & step control

## What it is
The numerical core: a high-order Taylor stepper, its order/tolerance knobs, and the automatic
time-step-control strategies that long-time integration builds on. (`odes_taylor.html` +
`odes_timemap.html` §"Error tolerance and step control" + `StepControl.h`.)

## Key API (confirmed in doc/header)
- Construct: `IOdeSolver solver(vectorField, order)`. `order` is the Taylor truncation order
  (examples use 20-30). `setStep(h)` fixes a step **and turns off step control**;
  `turnOnStepControl()` / `turnOffStepControl()` / `onOffStepControl(bool)` toggle it.
- **Step control** (confirmed `StepControl.h`): `setAbsoluteTolerance` / `setRelativeTolerance`
  bound the per-step Lagrange-remainder norm; `setMaxStep` caps `h`. The predictor
  `computeNextStep(solver, numberOfTerms, epsilon, minTimeStep, degree)` picks
  `h = min_i (epsilon / ‖coeff_i‖)^{1/i}` over the last `numberOfTerms` Taylor terms, clamped
  to `[minTimeStep, maxStep]`, with `clearMantissaBits` to land on clean step values.
- **Step-control policies** (header-confirmed classes):
  - `ILastTermsStepControl(_terms=1, _minStep=1/2²⁰)` — **default for `IOdeSolver`/`IC2OdeSolver`**;
    uses the last `_terms` Taylor terms.
  - `IEncFoundStepControl(minStep=1/2²⁰, stepFactor=0.25)` — **default for `ICnOdeSolver`**;
    shrinks `h` by ×0.8 until `solver.enclosure(t,x)` validates (enclosure-driven).
  - `FixedStepControl`, `NoStepControl` — fixed / disabled.
- Doc warning: low Taylor order + very small tolerances forces tiny steps → slow.

## dReal status
**Used (a subset).** dReal sets order (`--ode-taylor-order` 12 fwd / `--ode-backward-order`),
`setAbsoluteTolerance`/`setRelativeTolerance` (both 1e-10), and `setMaxStep` only when
`--ode-max-step > 0` (else adaptive) (dreal-capd-usage.md §integration path 2). It does **not**
choose a step-control policy (uses the `ILastTermsStepControl` default with `_terms=1`).

## Why it might matter
- `ILastTermsStepControl`'s `_terms` defaults to **1** for interval solvers — a single-term
  predictor can over-predict when the last coefficient is near zero (the header's own remark
  warns of this for the nonrigorous `DLastTermsStepControl` default of 2). Setting `_terms` to
  2-3 via `setStepControl` is a cheap robustness/tightness experiment dReal has not tried.
- `IEncFoundStepControl` ties the step directly to enclosure success — potentially fewer
  thrown steps on stiff/singular flows (the exact case dReal catches and abandons narrowing).
- `minTimeStep = 1/2²⁰ ≈ 9.5e-7` is the floor below which the solver gives up; relevant to
  dReal's catch-all-skip path on hard flows.

## Source
[odes_taylor.html](../../../CAPD/docs/html/odes_taylor.html) ·
[StepControl.h](../../../CAPD/docs/html/StepControl_8h.html)
