# Geomsets — the storage form under every dynset

## What it is
`geomset` classes hold the **pure geometry** of a propagated set, with no dynamics attached. Every `dynset` (the thing that actually moves) inherits its representation from a `geomset` base and adds a `move(dynsys)` algorithm. Understanding the geomset forms explains *what the symbols in the dynset names mean*.

## The two forms (from `geomset_module.html`)
- **Affine**: `x + B*r` — center `x`, one coordinate frame `B`, size vector `r`. Base of `C0RectSet` / `C0PpedSet`.
- **Doubleton**: `x + C*r0 + B*r` — center `x`; `r0` = initial set size in frame `C`; `r` = container for *all computational errors* (round-off, method error) in frame `B`. Base of `C0Rect2Set`.

`Centered*` variants additionally check in their constructors whether `r` (and `r0`) contain 0 and correct the representation if needed — keeping the center honestly inside the set.

## Why two frames
The whole point of the doubleton (vs. a plain interval box) is the **`C`/`B` split**: the *initial size* and the *accumulated error* deform differently under the flow, so carrying each in its own frame keeps both tight. Merging them — what a naive interval box does — is precisely what makes the wrapping effect compound. The tripleton goes one further, intersecting two frames on the error part.

## Accessors
No direct field access; `get_m()/set_m()` per member `m ∈ {x, r, r0, B, C}`, plus `getElement_m(i)`, `getRow_m(i)`, `getColumn_m(j)`, `setElement_m(i,v)`, etc. (0-indexed). dReal never uses these — it only constructs from / casts to `IVector`.

## dReal status
**Indirect.** No geomset class is named in dReal source; they are the (invisible) base classes of the three wired C0 sets. Relevant only as background to read the dynset names.

## Source
[group__geomset.html](../../../CAPD/docs/html/group__geomset.html) · [geomset_module.html](../../../CAPD/docs/html/geomset_module.html) · [DoubletonSet](../../../CAPD/docs/html/classcapd_1_1geomset_1_1DoubletonSet.html) · [AffineSet](../../../CAPD/docs/html/classcapd_1_1geomset_1_1AffineSet.html)
