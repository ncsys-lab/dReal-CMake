# OneDimKSSineVectorField

## What it is
The **only concrete dissipative PDE shipped with CAPD** — the worked example. Implements the
vector field of the 1-D real **Kuramoto–Sivashinsky** equation as a `DissipativeVectorField<
GeometricBound<interval>>`, under fixed modeling assumptions (header comment): solutions are
**periodic and odd**, so only sine Fourier components appear,
`u(t,x) = -2 Σ_{k≥1} a_k(t) sin(kx)`, and coefficients are required to have geometric-like decay
`|a_k| ≤ C/(q^k k^s)`, `q>1` (an analytic, representable subset). The single physical parameter
is the viscosity `ν`.

## Key API (confirmed in `OneDimKSSineVectorField.h`)
- `OneDimKSSineVectorField(ScalarType nu, size_type dim, size_type firstDissipativeVariable)` —
  `dim` leading modes; `firstDissipativeVariable` sets the dissipative-mode boundary.
- Implements the full `DissipativeVectorField` contract: `operator()(h,v)` / `(h,v,DF)`,
  `derivative(h,v)`, both `computeODECoefficients` overloads, both `makeSelfConsistentBound`
  overloads, `dimension()`, `firstDissipativeIndex()`, both `updateTail` overloads, `blockNorms`.
- KS-specific: `setParameter(nu)` and `getLambda(k)` — the linear eigenvalues
  `λ_k = k²(1 − ν·k²)` (confirmed in `setParameter`); for large `k` these go strongly negative,
  which is exactly the dissipativity the method exploits.
  `computeD1D2DI(a)` returns the constants used to bound the nonlinear/infinite part.

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** No `src/` reference.

## Why it might matter
This class is the template a user would copy to add any *other* dissipative PDE to dReal: it shows
exactly how much must be supplied per equation — the ODE-coefficient recursions, the
self-consistent (isolation) bound, the linear-differential-inequality tail update, and the block
norms — far more than the expression-tree `IMap` the ODE path builds automatically
([dreal-capd-usage.md](../../dreal-capd-usage.md)). The fact that KS is the *only* shipped field
is the clearest evidence of CAPD's narrow PDE scope: the framework is general over
`DissipativeVectorField`, but every concrete instance is hand-derived mathematics.

## Source
[classcapd_1_1pdes_1_1OneDimKSSineVectorField.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1OneDimKSSineVectorField.html)
(header: `capd/pdes/OneDimKSSineVectorField.h`)
