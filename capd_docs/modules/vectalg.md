# vectalg (module)

## What it is
CAPD's vector/matrix algebra layer: container-backed `capd::vectalg::Vector<Scalar,dim>` and
`capd::vectalg::Matrix<Scalar,...>`, the no-own-container views (`ColumnVector`, `RowVector`,
`MatrixSlice`), norm classes, and a family of generic interval-object algorithms that work
uniformly over vectors and matrices. The `capdlib.h` aliases `DVector`/`LDVector`/`IVector`
and `DMatrix`/`LDMatrix`/`IMatrix` instantiate this over `double`, `long double`, and `interval`.

## Key API (from group__vectalg.html / linear_algebra.html, confirmed in `vectalg/`)
- Containers: `Vector<Scalar,dim>`, `Matrix<Scalar,r,c>`; `makeArray(N,dim)` /
  `makeArray(N,r,c)` static allocators; zero-based `operator[]`, `m[i][j]`.
- Overloaded math: matrix*matrix, matrix*vector, vector scalar-product (`x*y`), scalar*vector,
  vector +/-.
- Generic interval-object algorithms (template over any vector/matrix of intervals):
  `split` (center + [-r,r]), `maxDiam`, `maxWidth`, `containsZero`, `subset`,
  `subsetInterior`, `intersection`, `intersectionIsEmpty`, `intervalHull`, `diameter`, `mid`,
  `midObject`, `leftObject`.
- Norm classes (operator norms): `DEuclNorm`/`IEuclNorm`, `DMaxNorm`/`IMaxNorm`,
  `DSumNorm`/`ISumNorm`.

## dReal status
**Used (partial).** `capd::IVector` is on dReal's referenced-symbols list (`dreal-capd-usage.md`):
ODE enclosures come back as interval vectors that dReal writes into its Box. The matrix views,
norm classes, and the generic set algorithms are **not** called by dReal directly — dReal's own
contraction logic lives in ibex.

## Why it might matter
`subsetInterior` / `containsZero` / `maxDiam` are the building blocks of CAPD's own rigorous
self-validation inside the Taylor stepper; they are not a dReal lever. If dReal ever wanted to
post-process CAPD enclosures (e.g. measure tube width for adaptive slicing) these are the
ready-made rigorous primitives.

## Source
[group__vectalg.html](../../../CAPD/docs/html/group__vectalg.html)
