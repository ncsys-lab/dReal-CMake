# Module: pdes

## What it is
The `capd::pdes` module provides **rigorous integration of dissipative PDEs**, treated as
*infinite-dimensional ODEs* in a Fourier (or similar) spectral basis. A state is split into a
finite block of explicitly-tracked **leading modes** (a Galerkin projection) plus an infinite
**tail** whose coefficients are bounded uniformly by a decaying series; the solver advances
both rigorously, exploiting *dissipativity* (high modes are strongly damped) to keep the tail
enclosure from blowing up. The module's worked example is the 1-D Kuramoto–Sivashinsky
equation (`OneDimKSSineVectorField`).

This is the PDE analogue of the `odes`/`dynsys` machinery: `PdeSolver` plays the role of
`OdeSolver`, the `*GeometricTail` doubleton sets play the role of the `dynset` C0/C1 sets, and
the tail-bound types (`GeometricBound`/`PolynomialBound`/`PolyLogBound`) are the
infinite-dimensional vector representation.

## Scope boundary (read this first)
CAPD's PDE support is **narrow and specific**: rigorous time-integration of **dissipative
semilinear PDEs** whose solutions are analytic and admit a self-consistent decaying-tail
representation in a spectral basis. It is **NOT** a general PDE solver. Confirmed by grepping
the installed `capd/pdes` headers — there is **no** class for:
- elliptic / hyperbolic / general parabolic boundary-value problems,
- the method of lines, finite elements, finite differences, or finite volumes,
- general spatial-domain discretization (only a fixed spectral/Fourier representation appears).

The only vector field shipped is `OneDimKSSineVectorField` (Kuramoto–Sivashinsky); supporting
any other PDE requires implementing the `DissipativeVectorField` interface for that system.
Treat the module as "rigorous integrator for one well-behaved class of PDEs," not as broad
PDE coverage.

## Classes (from `namespacecapd_1_1pdes.html` + headers)
- **`PdeSolver<SeriesT, StepControlT>`** — the one-step rigorous integrator (Taylor method +
  high-order enclosure + tail update). Analogue of `OdeSolver`.
- **`DissipativeVectorField<SeriesT>`** — abstract interface a PDE must implement to be
  integrable (ODE coefficients, self-consistent bound, tail update, block norms).
- **`OneDimKSSineVectorField`** — the only concrete field: 1-D Kuramoto–Sivashinsky, odd/periodic,
  sine-Fourier basis.
- **Tail-bound (infinite-dim vector) types**: `GeometricBound<ScalarT>` (`|x_i| ≤ C·q^{-i}`),
  `PolyLogBound` (`|x_i| ≤ C·q^{-i}·i^{-d}`), `PolynomialBound<Scalar,Exponent,M>`.
- **PDE sets** (initial-condition representations propagated by `PdeSolver`):
  `C0DoubletonSetGeometricTail<BaseT>`, `C0HODoubletonSetGeometricTail<BaseSetT>`,
  `C1DoubletonSetGeometricTail<Policies>`.
- **`PdeCurve<SeriesT>`** — Taylor-curve data structure (base of `PdeSolver`).
- **PDE Poincaré sections**: `PdeAbstractSection<VectorT,MatrixT>`, `PdeAffineSection`,
  `PdeCoordinateSection` (+ helpers `PdeSectionDerivativesEnclosure`,
  `ComputeOneStepSectionEnclosure`).

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** `grep` over `src/` finds
zero references to `capd/pdes`, `PdeSolver`, `DissipativeVectorField`, `GeometricBound`, or
`OneDimKS`. dReal's ODE path binds only `capd::IMap`/`IOdeSolver`/`ITimeMap` and the C0
`dynset` types (`dreal-capd-usage.md`).

## Why it might matter
If dReal ever supported a dissipative-PDE constraint (e.g. a `forall_t` invariant over a KS
trajectory), this module is the rigorous-enclosure engine. Concept node:
[dissipative-pdes](../concepts/dissipative-pdes.md); per-class detail in
[classes/pdes/](../classes/pdes/).

## Source
[group__pdes.html](../../../CAPD/docs/html/group__pdes.html) ·
[namespacecapd_1_1pdes.html](../../../CAPD/docs/html/namespacecapd_1_1pdes.html)
