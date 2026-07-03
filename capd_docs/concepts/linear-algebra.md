# Linear algebra (concept)

## What it is
CAPD's vector/matrix layer (`vectalg`) plus rigorous and non-rigorous algorithms over it
(`matrixAlgorithms`, `alglib`). Types come from `capd/capdlib.h`: `DVector`/`IVector`,
`DMatrix`/`IMatrix` (and long-double variants), with operator-norm classes
`*EuclNorm`/`*MaxNorm`/`*SumNorm`. Vectors/matrices are zero-indexed; math operators
(`A*B`, `A*x`, `x*y` scalar product, `+`/`-`/scalar*) are overloaded.

## Rigorous vs. non-rigorous
- **Interval (rigorous)** matrices support enclosure algorithms: `gauss(A,b)` solves `Ax=b`,
  `gaussInverseMatrix`, and `krawczykInverse` — the docs recommend `krawczykInverse` for interval
  matrices because it yields *tighter* enclosures. `QR_decompose`/`orthonormalize` underpin the
  QR set reorganizations CAPD uses against the wrapping effect. These throw `std::runtime_error`
  on failure.
- **Non-interval only:** eigenvalues/eigenvectors (`alglib::computeEigenvalues…`),
  `symMatrixDiagonalize`, `spectralRadiusOfSymMatrix`, `maxEigenValueOfSymMatrix` (Gerschgorin),
  `matrixExp`. The docs state eigen-routines are implemented for non-interval matrices only.

## dReal relevance
**Largely unused by dReal.** Only `IVector` is on dReal's referenced-symbol list
(`dreal-capd-usage.md`) — ODE enclosures arrive as interval vectors and are written into dReal's
Box; the matrices, norms, and matrix algorithms are exercised *inside* CAPD's integrator, not by
dReal. dReal uses C0 (value-only) sets, so the QR/Krawczyk machinery (which matters for C1/Cn
variational integration) is dormant. It would become relevant only if dReal adopted C1 sets to
sharpen backward X₀ narrowing — flagged as audit territory in `dreal-capd-usage.md`.

## Source
[linear_algebra.html](../../../CAPD/docs/html/linear_algebra.html),
[group__vectalg.html](../../../CAPD/docs/html/group__vectalg.html),
[group__matrixAlgorithms.html](../../../CAPD/docs/html/group__matrixAlgorithms.html)
