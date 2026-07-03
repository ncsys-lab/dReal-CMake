# matrixAlgorithms (module)

## What it is
Rigorous and non-rigorous linear-algebra algorithms over CAPD matrices, in namespace
`capd::matrixAlgorithms` (`matrixAlgorithms/floatMatrixAlgorithms.h`). These underpin the
QR-based set representations CAPD uses to fight the wrapping effect during integration.

## Key API (signatures from `floatMatrixAlgorithms.h`; `krawczykInverse` from linear_algebra.html)
- `void gauss(MatrixType a, ResultType b, ResultType& result)` and
  `VectorType gauss(const MatrixType& A, const VectorType& b)` — solve `A x = b`.
- `void QR_decompose(const MatrixType& A, MatrixType& Q, MatrixType& R)`.
- `void orthonormalize(MatrixType& Q)` (+ overloads with `v` / with `R`).
- `MatrixType gaussInverseMatrix(const MatrixType& A)`, `MatrixType inverseMatrix(const MatrixType&)`.
- `krawczykInverse(M)` — rigorous enclosure of an interval-matrix inverse; the docs **recommend
  it over `gaussInverseMatrix` for interval matrices because it gives tighter enclosures**
  (linear_algebra.html). Declared for interval matrices in `vectalg/Matrix_Interval.hpp`, not in
  `floatMatrixAlgorithms.h` — confirm exact signature there if called.
- `int symMatrixDiagonalize(...)`, `spectralRadiusOfSymMatrix(A, relTol)`,
  `maxEigenValueOfSymMatrix(A, relTol)` (Gerschgorin bound), `MatrixType matrixExp(M, tol)`.
- `croutDecomposition(A,&D,&G)`, `invLowerTriangleMatrix`, `invUpperTriangleMatrix`.
- Failure mode: these throw `std::runtime_error` when they cannot complete (linear_algebra.html).
- Eigenvalue/eigenvector routines for **non-interval matrices only** (see `alglib` module).

## dReal status
**Unused (directly).** Not on dReal's referenced-symbol list (`dreal-capd-usage.md`). dReal uses
only C0 (value-only) sets; these algorithms are invoked internally by CAPD's C1/Cn solvers and
QR set reorganizations, which dReal does not enable.

## Why it might matter
If dReal ever adopted C1 variational integration (to sharpen backward X₀ narrowing — flagged as
audit territory in `dreal-capd-usage.md`), the wrapping-effect fight runs through `QR_decompose`
/ `orthonormalize` / `krawczykInverse`. Not a lever under the current C0-only path.

## Source
[group__matrixAlgorithms.html](../../../CAPD/docs/html/group__matrixAlgorithms.html)
