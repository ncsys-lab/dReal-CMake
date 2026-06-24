# C0DoubletonSet

## What it is
A C0 (position-only) set stored as a **doubleton**: `x + C*r0 + B*r` — center `x`, initial size `r0` in frame `C`, accumulated errors `r` in frame `B` (confirmed `C0DoubletonSet.h` / class detail). Moved by re-orthogonalizing/reorganizing the frames each step. This is the concrete class behind dReal's default `C0Rect2Set`.

## Key API
- Constructors: `C0DoubletonSet(x[, r0][, C][, B][, r][, t])` — progressively fuller specs; omitted matrices default to identity, vectors to 0.
- `move(DynSysType& dynsys)` and `move(dynsys, C0DoubletonSet& result)` — propagate one solver step (in place or into `result`).
- `operator VectorType()` — cast to the enclosing `IVector` (what dReal reads per slice).
- `affineTransformation(M, x0)` — image of affine map `M*(X-x0)` computed in the doubleton frames: `M*(x-x0) + (M*C)*r0 + (M*B)*r` (tighter than transforming the cast IVector).
- Template params: `<MatrixT, Policies>` — `Policies` carries the QR + reorganization choice.

## dReal status
**Used (default).** `--ode-c0-set rect2` selects `C0Rect2Set = C0DoubletonSet<IMatrix, FactorReorganization<FullQRWithPivoting<>>>` (`dynset/typedefs.h:24,45`; `dreal-capd-usage.md`). The cheapest of the three wired sets.

## Why it might matter
The baseline wrapping-fighter: two frames + QR re-orthogonalization. Cheapest per step but the **loosest** of the three wired sets for strongly deforming flows — where tripleton/HO's extra intersection pays off. dReal defaults here even though CAPD's own default is the tripleton, which is a deliberate speed-over-tightness choice worth re-examining per benchmark family.

## Source
[classcapd_1_1dynset_1_1C0DoubletonSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0DoubletonSet.html)
