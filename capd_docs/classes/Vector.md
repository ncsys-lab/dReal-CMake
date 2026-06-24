# Vector

## What it is
`capd::vectalg::Vector<Scalar, dim>` — CAPD's container-backed vector. Instantiated as `DVector`
(double), `LDVector` (long double), and `IVector` (interval) via `capd/capdlib.h`. `dim` may be a
compile-time constant or `CAPD_DEFAULT_DIMENSION (0)` for runtime-sized vectors.

## Key API (from linear_algebra.html, confirmed in `vectalg/Vector.h`)
- Construction: `IVector v(n)` (n zeros), `IVector v(n, coeffArray)`,
  `static makeArray(N, dim)` for arrays of vectors.
- Indexing: zero-based `operator[]`; implicit `double → interval` conversion on assignment.
- Math: vector +/-, scalar*vector, `x*y` (scalar product), `A*x` (matrix*vector).
- Generic interval-vector algorithms (free functions, `vectalg`): `split`, `maxDiam`, `mid`,
  `midObject`, `containsZero`, `subset`, `subsetInterior`, `intersection`, `intervalHull`.
- Operator-norm classes: `IEuclNorm`, `IMaxNorm`, `ISumNorm`.

## dReal status
**Used (partial).** `capd::IVector` is on dReal's referenced-symbol list (`dreal-capd-usage.md`):
ODE enclosures return as interval vectors written into dReal's Box. The norm classes and most
generic algorithms are not called by dReal.

## Why it might matter
`IVector` is the hand-off boundary between CAPD enclosures and dReal's Box. If dReal post-processed
tubes (e.g. width-driven adaptive slicing), `maxDiam`/`split`/`subsetInterior` are the ready
rigorous primitives — currently unused.

## Source
[linear_algebra.html](../../../CAPD/docs/html/linear_algebra.html)
