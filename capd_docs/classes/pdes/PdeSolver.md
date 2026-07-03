# PdeSolver

## What it is
The one-step **rigorous integrator** for a dissipative PDE-as-infinite-dim-ODE — the PDE
analogue of `OdeSolver`. Template `PdeSolver<SeriesT, StepControlT>`; derives from
`PdeCurve<SeriesT>` (the Taylor-coefficient store) and a `StepControlInterface`. `SeriesT` is a
tail-bound vector type (e.g. `GeometricBound<interval>`); the vector field is a
`DissipativeVectorField<SeriesT>`. Default step control is `ILastTermsStepControl` (confirmed
`PdeSolver.h`).

## Key API (confirmed in `PdeSolver.h`)
- `PdeSolver(VectorFieldType& f, size_type order)` — order ≥ 1 enforced (throws otherwise).
- `void encloseC0Map(x0, x, y, o_phi, o_rem, o_enc, o_jacPhi)` — one C^0 step: outputs the
  numerical value `φ(x0)`, the method remainder, the **enclosure of all trajectories over the
  step** (`o_enc`), and a finite Jacobian block. (Param semantics from the header's inline doc.)
- `void encloseC1Map(...)` — C^1 step; additionally propagates the variational (Jacobian) blocks
  including the `Dxy`/`Dyy` infinite-block norms via `blockNorms` + `matrixExp`.
- `void computeImplicitCoefficients(x0, x, q)` — implicit Taylor coefficients.
- `operator()(SetType& set)` / `operator()(set, result)` — drives a set's `move()` (this is how
  the `*GeometricTail` sets are stepped).
- Curve access: `getCurve()`, `getImplicitCurve()`; step control: `setStep`, `getStep`,
  `adjustTimeStep`, `computeTimeStep`; `getVectorField()`, `getCoeffNorm`.
- Internals worth naming (they encode the method): `highOrderEnclosure` (self-consistent
  enclosure search, throws `SolverException` on failure), `predictEnclosure` (splits
  non-dissipative vs dissipative modes at `firstDissipativeIndex()`), `makeSelfConsistentBound`,
  `checkRemainderInclusion`.

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** dReal binds `capd::IOdeSolver`
for finite ODEs ([dreal-capd-usage.md](../../dreal-capd-usage.md)); `grep src/` finds no
`PdeSolver`.

## Why it might matter
Supporting a dissipative-PDE constraint in dReal would put `PdeSolver` exactly where
`IOdeSolver` sits today in `contractor_odes_capd.cc`: construct once per flow, step adaptively to
`t_ub`, read per-step enclosures into the box. The `o_enc` "enclosure of all trajectories over
the step" is the direct analogue of the ODE tube slice used for `forall_t` invariant refutation.

## Source
[classcapd_1_1pdes_1_1PdeSolver.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1PdeSolver.html)
(header: `capd/pdes/PdeSolver.h`)
