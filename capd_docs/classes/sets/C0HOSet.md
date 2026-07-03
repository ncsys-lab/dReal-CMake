# C0HOSet

## What it is
A **higher-order** wrapper, `C0HOSet<BaseSetT>`, that re-uses the geometric representation of its base set (a doubleton or tripleton) but computes each ODE step **twice** — by the **Taylor method and the Hermite–Obreshkov (HO) method** — and takes the **intersection** of the two enclosures (confirmed `C0HOSet` detail / `C0HOSet.h`). HO is an implicit, higher-order-accurate scheme, so its bound on the per-step *time remainder* is tighter; the intersection inherits the smaller.

## Key API
- Constructors mirror the base set: `C0HOSet(x[, C][, r0][, B][, r][, t])`, or `C0HOSet(const BaseSet&)`.
- `move(Solver&)` / `move(Solver&, C0HOSet& result)` — one step; internally fills `predictor`/`corrector` base-set copies and intersects.
- `evalAt(f)` — value of functor `f` as the **intersection of predictor and corrector** evaluations (the HO tightening, exposed for functionals).
- `affineTransformation(M, x)`, `operator VectorType()` — enclosure in canonical coordinates.
- `computeC0HORemainder(p, q)`, `degree()` — HO order controls.
- **Order cap:** Taylor order ≤ 64 for `C0HOSet` (≤ 32 for `C0HODoubletonSet`), limited by binomial-coefficient integer capacity; minimum order 3.

## dReal status
**Used.** `--ode-c0-set horect2` selects `C0HORect2Set = C0HOSet<C0Rect2Set>` (`dynset/typedefs.h:47`; `dreal-capd-usage.md`). The other instantiation, `C0HOTripletonSet = C0HOSet<C0TripletonSet>` (`typedefs.h:48`), exists in CAPD but is **not wired** by dReal — a potential "tightest available" combination (tripleton frames + HO time bound).

## Why it might matter
Best when the **time-discretization remainder dominates** the tube width — long integrations, high-curvature flows. The cost is ~2× integration per step, so it is a tightness-for-speed trade. The unexposed `C0HOTripletonSet` would stack both tightening mechanisms; worth a benchmark if HO already helps on a family.

## Source
[classcapd_1_1dynset_1_1C0HOSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0HOSet.html)
