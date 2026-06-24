# Class: `capd::dynsys::BasicOdeSolver`

## What it is
The base Taylor stepper that `OdeSolver` derives from (`OdeSolver::BaseTaylor`). It holds the
order/step/tolerance state, the Taylor-coefficient machinery, and the `SolutionCurve`; the
rigorous `enclosure`/`encloseC0Map` overrides live in the derived `OdeSolver`. The
**nonrigorous** `DOdeSolver` (= `BasicOdeSolver<DMap>`) is this class directly. Template:
`BasicOdeSolver<MapT, StepControlT = DLastTermsStepControl, CurveT>`.

## Key API (confirmed in `BasicOdeSolver.h` page)
- `setOrder(order)`, `getStep()`, `setStep(h)` (fixes step, turns off control),
  `adjustTimeStep(h)`.
- `enclosure(t, x)` (base declaration), `operator()(VectorType)` / `(t, u)` — one step of a
  point/vector; `operator()(v, MatrixType& o_resultDerivative)` and `(v, derivative, o_…)` —
  step plus flow-derivative; `operator()(JetT& jet)` — jet step.
- `getCurve()` → `SolutionCurve`; `getStepControl()`, `setStepControl(const StepControlType&)`.
- Jet mask: `setMask`, `addMultiindexToMask`, `resetMask`.

## dReal status
**Indirectly used.** dReal uses the derived `IOdeSolver`; `BasicOdeSolver` supplies its
order/step/curve plumbing. dReal does not construct a `BasicOdeSolver` (nonrigorous `DOdeSolver`)
itself.

## Why it might matter
Marginal for dReal directly. Documented here because `OdeSolver`'s inherited members
(`getStep`, `getCurve`, `setStepControl`, mask methods) resolve to this base — useful when
reading which knob lives where. The nonrigorous `DOdeSolver` could only serve as a fast,
**unsound** pre-filter, which contradicts dReal's soundness mandate — not a recommendation.

## Source
[BasicOdeSolver.h](../../../../CAPD/docs/html/BasicOdeSolver_8h.html) ·
[class page](../../../../CAPD/docs/html/classcapd_1_1dynsys_1_1BasicOdeSolver.html)
