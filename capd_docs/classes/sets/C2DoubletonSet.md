# C2DoubletonSet

## What it is
A **C2** set in doubleton form — position, the flow **Jacobian** `∂φ/∂x₀`, **and** the
**Hessian** `∂²φ/∂x₀²` all carried and propagated, via the **C²-Lohner algorithm**
(confirmed `C2DoubletonSet.h` / "C2 set in doubleton form. C^2-Lohner algorithm."). The C0
part is a `CenteredDoubletonSet`, the C1 part a `MatrixDoubletonSet`, and the second-order
part a Hessian doubleton (confirmed class detail). Representative of the C2 tier
(`C2Rect2Set`, `C2Pped2Set`).

## Key API
- Constructors `<MatrixT, Policies>` build up `(x[, C, r0[, B, r]][, H][, t])`, or from
  separate C0/C1 parts plus a `HessianType H` (confirmed class doc constructor list).
- `move(DynSysType& c2dynsys, C2DoubletonSet& result)` — needs at least a **C2** dynamical
  system.
- `operator VectorType()` (position) / `operator MatrixType()` (Jacobian) / a Hessian
  accessor — "returns an enclosure of second order derivative in the canonical coordinates".
- `affineTransformation(M, x0)` carries the doubleton frames through an affine map.

## dReal status
**Not used (structural).** dReal computes only C0 (value-only) tubes; the variational layer
(Jacobian, Hessian) is never requested (`dreal-capd-usage.md`).

## Why it might matter
The Hessian is curvature of the flow w.r.t. initial conditions — needed only for a
**second-order** mean-value / Newton narrowing of `X₀`. dReal does not even use the C1
Jacobian yet (`classes/sets/C1DoubletonSet.md` is the first structural step), so C2 is a
*further* step beyond that, justified only if a first-order C1 narrowing proves too loose on
high-curvature terminal gates. Cost is another order of jet integrated per step. Listed for
completeness of the catalog, not a near-term candidate.

## Source
[classcapd_1_1dynset_1_1C2DoubletonSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C2DoubletonSet.html)
