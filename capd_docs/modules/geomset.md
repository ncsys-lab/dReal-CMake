# Module: geomset

## What it is
The `capd::geomset` module holds the **geometry** of a set — the pure storage form and its accessors — with **no dynamics**. These classes are the base classes that `dynset` types inherit from; a `dynset` adds the `move(dynsys)` propagation algorithm on top of a `geomset`'s representation (`geomset_module.html`).

The module's framing of the core tradeoff: "a naive set representation (as a product of intervals) yields to big overestimations, on the other hand representations with polynomials of high degree are very accurate but when propagated they need a lot of computations." The doubleton form is the middle ground.

## Key classes
- `AffineSet` / `CenteredAffineSet` — form `x + B*r` (center `x`, frame `B`, size `r`). `Centered` variants check `r ∋ 0` in constructors and correct the representation.
- `DoubletonSet` / `CenteredDoubletonSet` — form `x + C*r0 + B*r` (two frames `C`, `B`; `r0` = initial size, `r` = accumulated computational errors). dReal's `C0Rect2Set` derives from `CenteredDoubletonSet`.
- `CenteredTripletonSet` — base of `C0TripletonSet` (third frame `Q*q`, intersected with `B*r`).
- `MatrixAffineSet`, `MatrixDoubletonSet` — matrix-valued analogues used by C1+ sets to store the variational (Jacobian) part.

## Constructors / accessors
- Full spec: `DoubletonSet(x, C, r0, B, r)`; omitted data defaults vectors→0, matrices→identity. Partial combos allowed: `(dim)`, `(v)`, `(x,r0)`, `(x,C,r0)`, `(x,C,r0,r)`, `(x,C,r0,B,r)`.
- No direct field access; use `get_m()/set_m()` for each member `m ∈ {x, r, r0, B, C}`, plus `getElement_m(i)`, `getRow_m/getColumn_m`, `setElement_m`, etc. (0-indexed).

## dReal status
**Indirectly used** — dReal never names a `geomset` class, but every wired C0 set inherits its storage from one (`C0Rect2Set` → `CenteredDoubletonSet`, `C0TripletonSet` → `CenteredTripletonSet`). dReal touches these only through the `(IVector)set` cast and constructor, not via the get/set accessors.

## Why it might matter
Mostly background: explains *what* `x + C*r0 + B*r` means and *why* two frames exist (separating the propagated initial size from accumulated round-off/method error is what makes reorganization possible). Not itself a tuning surface.

## Source
[group__geomset.html](../../../CAPD/docs/html/group__geomset.html) · [geomset_module.html](../../../CAPD/docs/html/geomset_module.html)
