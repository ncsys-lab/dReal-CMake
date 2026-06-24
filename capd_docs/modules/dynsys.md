# Module: DynSys / ODEs — interval-based methods (`group__dynsys`)

## What it is
The CAPD::DynSys library group covering rigorous interval ODE integration: the
`OdeSolver` family (C0/C1/C2/Cn Taylor + Hermite-Obreshkov steppers), the C0/C1/C2/Cn
set representations (doubleton/tripleton enclosures), and step-control. This is the core
machinery dReal binds for its ODE contractor.

## Key API (confirmed in doc/header)
Three IVP-solving strategies (from `odes_rigorous.html`):
- **one-step method** — `set.move(solver)` advances one rigorous Taylor/HO step.
- **long-time integration** — `ITimeMap` over `OdeSolver` with automatic step control.
- **solution curve** — a `SolutionCurve` functional object over a time range, evaluable at
  any intermediate `t` without re-integration.

Set representations for the initial condition (all convertible to `IVector`; affine
3-arg constructor `(x, C, r0)` available):
- `C0Rect2Set`, `C0HORect2Set` — doubleton (`x + C·r0 + r`); Rect2 = Taylor, HO = Taylor
  predictor + Hermite-Obreshkov corrector.
- `C0TripletonSet`, `C0HOTripletonSet` — tripleton (extra QR-organized term; tighter
  against the wrapping effect for wide initial sets).

The doc states tripleton/HO variants "proved to be most efficient in typical cases"; the
one-step method "sometimes performs better for very wide initial conditions with relatively
low order (3-5)."

C1/C2/Cn variational machinery (confirmed in `group__dynsys` detail + headers): solvers
expose `encloseC1Map`/`encloseC2Map`/`encloseCnMap` that "Find enclosure for Jacobian matrix
(variational part) for whole time step" and "for second order variational equations."

## dReal status
**Partially used.** dReal binds `IOdeSolver` (= `OdeSolver<IMap>`), `ITimeMap`, and the
three C0 set types (`C0Rect2Set`/`C0HORect2Set`/`C0TripletonSet`) selected by `--ode-c0-set`
(dreal-capd-usage.md §Symbols). It uses the **long-time integration** strategy with
`stopAfterStep(true)`. The C1/C2/Cn variational solvers and the HO-tripleton set are bound by
the typedefs but **not** used by dReal.

## Why it might matter
The variational (C1+) enclosures give the flow's Jacobian ∂φ/∂x₀ rigorously — the lever for
interval-Newton / mean-value contraction on the initial-condition box (backward narrowing),
which dReal currently leaves entirely to ibex HC4 on C0 slice values. See
`../classes/solvers/C1OdeSolver.md`. The HO-tripleton set is a drop-in tighter enclosure
(completeness lever, never a soundness change).

## Source
[group__dynsys.html](../../../CAPD/docs/html/group__dynsys.html)
