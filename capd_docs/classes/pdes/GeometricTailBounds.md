# Geometric-Tail Bounds: GeometricBound · PolyLogBound · PolynomialBound

## What they are
The three **infinite-dimensional vector representations** used as the `SeriesT`/`VectorType` of
the PDE solver and sets. Each "represents a subset of a countable infinite-dimensional space"
(header comments) as a **finite block of explicit interval coefficients** `x_1,…,x_M` (the
leading/main modes) **plus a uniform decaying bound on the infinite tail** `x_i, i>M`. They
differ only in the tail's functional form. The `q>1` decay condition guarantees the represented
function is analytic near `t=0` (header comment) — the regularity the Taylor method needs.

## The three tail forms (confirmed in headers)
| Type | Tail bound `\|x_i\|` for `i>M` | State `(C,q,…)` |
|---|---|---|
| `GeometricBound<ScalarT=interval>` | `≤ C·q^{-i}`, `q>1` | `C, q` (`GeometricBound.h`) |
| `PolyLogBound` | `≤ C·q^{-i}·i^{-d}`, `q>1`, `d∈ℕ` | `C, q, d` (`PolyLogBound.h`) |
| `PolynomialBound<Scalar,Exponent,M>` | `≤ C/(i+1)^{exponent}` | `C, exponent` (`PolynomialBound.h`) |

`PolyLogBound` and `PolynomialBound` decay slower / more flexibly than the pure geometric form
(an extra polynomial or polylog factor), for fields whose modes do not decay purely
geometrically.

## Key API (confirmed in headers / `namespacecapd_1_1pdes.html`)
Common across all three:
- Construct `(dim, C, q[, d])`, optionally from an explicit coefficient array/vector.
- `getCoefficient(i)` — i-th coordinate for **any** `i` (explicit if `i≤M`, else the symmetric
  tail interval `±C·q^{-i}…`, computed inline in the header); `setCoefficient(i,·)` (explicit
  range only, throws past it).
- `getConstant`/`setConstant` (C ≥ 0 enforced), `getGeometricDecay`/`setGeometricDecay`
  (`q>1` enforced); `PolyLogBound` adds `getDegree`/`setDegree`.
- `getExplicitCoefficients`/`setExplicitCoefficients`, `projection(dim)`, `dimension()`,
  `operator[]`, `clear()`, `partialDerivative()` (autodiff in time).
- Free operators (`namespacecapd_1_1pdes.html`): `+`, `-`, scalar `*`, **matrix `*`** (linear
  change of leading coords, tail unchanged — throws if matrix dim > explicit-coeff count),
  `operator<<`, `intersection`, `split`, `midVector`, `swap`.
- `PolynomialBound` also exposes `computeQF`/`computeQI` (the convolution-type operators
  `QF_i = Σ_{k<i} y_k w_{i-k}`, `QI_i = Σ_k y_k w_{k+i}` — confirmed in `PolynomialBound.h`
  comments) for products of series.

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** No `src/` reference.

## Why it might matter
These are the data type a dissipative-PDE state would live in inside dReal: the finite block maps
to box variables (slice intervals, like the ODE tube), and `(C,q[,d])` is the extra rigorous
information bounding the un-modeled high modes. Reading a PDE slice back into the box means
extracting `getExplicitCoefficients()` plus the tail constants. The `intersection`/`split`
operators are the analogues of the interval-set operations the ICP loop already performs.

## Source
[classcapd_1_1pdes_1_1GeometricBound.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1GeometricBound.html) ·
[classcapd_1_1pdes_1_1PolyLogBound.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1PolyLogBound.html) ·
[classcapd_1_1pdes_1_1PolynomialBound.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1PolynomialBound.html)
(headers: `capd/pdes/{GeometricBound,PolyLogBound,PolynomialBound}.h`)
