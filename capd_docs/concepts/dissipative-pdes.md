# Rigorous Integration of Dissipative PDEs

How CAPD's `pdes` module rigorously integrates a dissipative PDE by treating it as an
**infinite-dimensional ODE**. This is a narrow capability — read the scope boundary at the
end before assuming general PDE support.

## The idea: PDE → infinite-dim ODE → finite block + rigorous tail
A dissipative semilinear PDE (the worked example is 1-D Kuramoto–Sivashinsky) is written in a
spectral basis. For KS, with odd/periodic solutions, `u(t,x) = -2 Σ_{k≥1} a_k(t) sin(kx)`, so
the state is the infinite coefficient sequence `(a_1, a_2, …)` and the PDE becomes an
infinite system of ODEs `da_k/dt = f_k(a)` (confirmed in `OneDimKSSineVectorField.h` header
comments). CAPD cannot store infinitely many intervals, so it splits the state two ways:

1. **Leading modes** — a finite block `a_1,…,a_M` carried explicitly as interval coefficients
   (a *Galerkin projection*). These are the directions where the dynamics is non-trivial.
2. **The tail** — all modes `a_i, i>M`, *not* enclosed individually but **uniformly bounded by
   a decaying series**. The three tail-bound types (concept node tells which to use):
   - `GeometricBound`: `|a_i| ≤ C·q^{-i}`, `q>1` (confirmed `GeometricBound.h`).
   - `PolyLogBound`: `|a_i| ≤ C·q^{-i}·i^{-d}` (confirmed `PolyLogBound.h`).
   - `PolynomialBound<…>`: tail `≤ C/(i+1)^exponent` (confirmed `PolynomialBound.h`).
   The `q>1` (geometric decay) condition is exactly what guarantees the represented function is
   *analytic* near `t=0` (header comment) — the regularity that makes the tail summable and the
   Taylor method valid.

A tail-bound object is therefore a single rigorous *vector in an infinite-dim space*: a finite
interval vector plus three scalars `(C, q[, d])`. Arithmetic on it (`+`, `−`, scalar/matrix `*`,
`intersection`, `split`, `partialDerivative`) acts explicitly on the leading modes and
propagates the decay constants on the tail (confirmed: the `operator+`/`operator*` doc in
`namespacecapd_1_1pdes.html` gives the exact `C_result`/exponent formulas).

## What "dissipative" buys: the tail contracts
In a dissipative PDE the linear part strongly damps high modes — for KS, `λ_k = k²(1 − ν·k²)`
becomes large-negative for large `k` (confirmed `OneDimKSSineVectorField::setParameter`). Above
a **first dissipative index** `m` (`firstDissipativeIndex()`, confirmed in
`DissipativeVectorField.h`), every mode is contracting. CAPD exploits this with a
**self-consistent / isolation** bound: `makeSelfConsistentBound` refines `(C,q)` until the
vector field *points inward* on the tail (the flow cannot push a tail coefficient out of its
bound). `PdeSolver::predictEnclosure` then treats non-dissipative modes with the usual
high-order Taylor enclosure and, on dissipative modes, relies on inward-pointing isolation; the
tail is advanced by `DissipativeVectorField::updateTail` via a *linear differential inequality*
(confirmed comments in both headers). Dissipativity is what stops the tail enclosure from
exploding the way a naive infinite-dim wrapping would — it is the structural property the whole
method depends on.

## The step: PdeSolver, the GeometricTail sets, and the curve
One rigorous step composes three pieces — the direct analogue of the ODE path
([odes-rigorous](odes-rigorous.md), [dynsets-and-wrapping](dynsets-and-wrapping.md)):

- **`PdeSolver<SeriesT>`** (analogue of `OdeSolver`): `encloseC0Map` produces, for one step,
  `φ(x0)`, the numerical-method remainder, an enclosure of all trajectories over the step, and a
  finite Jacobian block; `highOrderEnclosure` finds the self-consistent enclosure, and
  `m_vectorField.updateTail` advances the tail (confirmed `PdeSolver.h`). Built on `PdeCurve`
  (the Taylor-coefficient store).
- **The `*GeometricTail` doubleton sets** (analogue of the `dynset` C0/C1 sets): the
  initial-condition set, carried as `x + C·r0 + B·r` *on the leading block* (a doubleton — see
  [dynsets-and-wrapping](dynsets-and-wrapping.md)) **plus a geometric tail**.
  `C0DoubletonSetGeometricTail` derives from a `dynset::C0DoubletonSet` and adds the tail
  (`m_currentSeries`); its `move()` calls `dynsys.encloseC0Map(...)` then the inherited doubleton
  `move` for wrapping control (confirmed `C0DoubletonSetGeometricTail.h`).
  `C0HODoubletonSetGeometricTail` wraps a higher-order (`C0HOSet`) base; `C1DoubletonSetGeometricTail`
  additionally carries the variational (Jacobian) blocks.
- **Tail bounds** are the `VectorType` (`SeriesT`) those sets and the solver are templated on.

So the same wrapping-effect machinery (doubleton frames, QR/reorganization policies) protects
the finite leading block, while the dissipative tail bound handles the infinite remainder.

## Relation to dReal
**Unused — future direction (dReal has no PDE constraints today).** dReal's ODE contractor binds
the *finite* CAPD ODE path (`IMap`/`IOdeSolver`/`ITimeMap` + C0 `dynset` sets,
[dreal-capd-usage.md](../dreal-capd-usage.md)). Supporting a dissipative-PDE constraint would
mean swapping that for `PdeSolver` + a `*GeometricTail` set, feeding an SMT-described field
through the `DissipativeVectorField` interface, and reading slice enclosures (leading modes +
tail constants) back into the box — see [classes/pdes/](../classes/pdes/) and the per-class
"Why it might matter" notes. Soundness/completeness reasoning is identical to the ODE tube: a
looser enclosure only *fails to refute* a `forall_t` invariant
(COMPLETENESS, never a false `unsat`).

## Scope boundary (do not overstate)
This method applies to **dissipative semilinear PDEs with analytic solutions representable as a
decaying spectral tail** — and only those. Confirmed by grepping the installed `capd/pdes`
headers: there is **no** support for elliptic, hyperbolic, or general parabolic
boundary-value problems, the method of lines, finite elements/differences/volumes, or general
spatial discretization. The single shipped vector field is Kuramoto–Sivashinsky
(`OneDimKSSineVectorField`); any other PDE must be hand-implemented against
`DissipativeVectorField`. CAPD's PDE coverage is a *deep, narrow* tool, not broad PDE support.

## Source
[group__pdes.html](../../../CAPD/docs/html/group__pdes.html) ·
[namespacecapd_1_1pdes.html](../../../CAPD/docs/html/namespacecapd_1_1pdes.html) ·
[classcapd_1_1pdes_1_1PdeSolver.html](../../../CAPD/docs/html/classcapd_1_1pdes_1_1PdeSolver.html) ·
[classcapd_1_1pdes_1_1DissipativeVectorField.html](../../../CAPD/docs/html/classcapd_1_1pdes_1_1DissipativeVectorField.html)
