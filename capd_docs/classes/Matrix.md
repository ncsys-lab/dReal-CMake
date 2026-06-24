# Matrix

## What it is
`capd::vectalg::Matrix<Scalar, rows, cols>` — CAPD's container-backed matrix. Instantiated as
`DMatrix` (double), `LDMatrix` (long double), `IMatrix` (interval) via `capd/capdlib.h`.
Dimensions may be compile-time or runtime (`CAPD_DEFAULT_DIMENSION 0`). Related no-own-container
views: `ColumnVector`, `RowVector`, `MatrixSlice`.

## Key API (from linear_algebra.html, confirmed in `vectalg/Matrix.h`)
- Construction: `IMatrix m(r,c)` (zeros), `DMatrix m(r,c, coeffArray)` (row-major),
  `static makeArray(N, r, c)`.
- Indexing: zero-based `m[i][j]`.
- Math: `A*B`, `A*x`, scalar*matrix, +/-; `transpose(A)`.
- Linear algebra (`matrixAlgorithms`, throw `std::runtime_error` on failure):
  `gauss(A,b)`, `gaussInverseMatrix(A)`, `krawczykInverse(M)` (tighter for interval matrices),
  `QR_decompose(A,&Q,&R)`, `orthonormalize`. Eigen-routines are non-interval only.

## dReal status
**Unused (directly).** Not on dReal's referenced-symbol list (`dreal-capd-usage.md`). dReal uses
C0 (value-only) sets; matrices appear only inside CAPD's C1/Cn solvers and QR set reorganizations,
which dReal does not enable.

## Why it might matter
The interval-matrix path (`krawczykInverse`, `QR_decompose`) is the wrapping-effect machinery
behind C1 variational integration — the route to sharper backward X₀ narrowing flagged in
`dreal-capd-usage.md`. Dormant under the current C0-only configuration.

## Source
[linear_algebra.html](../../../CAPD/docs/html/linear_algebra.html)
