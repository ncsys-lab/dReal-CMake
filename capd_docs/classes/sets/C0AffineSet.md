# C0AffineSet

## What it is
A C0 set in the **single-frame** form `x + B*r` — center `x`, one coordinate frame `B`, size `r` (confirmed `C0AffineSet.h` / class detail). The minimal moving-frame representation; the base for the non-doubleton `C0RectSet` and `C0PpedSet` library facades.

## Key API
- Constructors `<MatrixT, Policies>`: from `(dim)`, `(v)`, `(x, r)`, `(x, B, r)`.
- `move(DynSysType&)` / `move(dynsys, C0AffineSet& result)`.
- `evalAt(f)` — `f(x) + (Df(X)*B)*r` (mean-value form over the single frame).
- `affineTransformation(M, x0)`, `operator VectorType()`.

## dReal status
**Not used.** dReal wires only the doubleton/tripleton/HO C0 sets, none of which is a plain `C0AffineSet`. (Its policy sibling `C0PpedSet = C0AffineSet<IMatrix, C0PpedPolicies>` is what implements the *Pped method*, also unwired.)

## Why it might matter
The single frame `B*r` cannot separate initial-size from accumulated error, so it wraps faster than a doubleton on most flows — generally *worse* for tightness, which is why dReal skips it. Its relevance is as the carrier of the **Pped method** (`C0PpedSet`): per CAPD's notes, parallelepiped frames beat orthogonalized-rect ones **near an elliptic fixed point with equal-magnitude eigenvalues**, but "blow up quickly" in general because contracting directions drive the frame toward singular. A niche, not a default candidate.

## Source
[classcapd_1_1dynset_1_1C0AffineSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0AffineSet.html)
