# `IntervalVector` — a box

Header:
[`ibex_IntervalVector.h`](../../../../ibex-fork/src/arithmetic/ibex_IntervalVector.h)
(`arithmetic/`). A vector of [`Interval`](Interval.md)s — IBEX's "box", the object
every contractor contracts in place. **Not** an inheritance subtype of `Interval`
(flat design, for speed); the empty box is *typed and sized*.

> **dReal status:** used pervasively — it is the representation a `contract(box)`
> call mutates. dReal's own `Box` abstraction sits above it.

## API surface (from the [interval chapter](../../chapters/interval.md))

- **Container:** `size() [i] subvector(i,j) resize(n) put(i,y) clear() init(y)`.
- **Geometry:** `lb() ub()` (corner vectors), `diam()` (vector), `min_diam()
  max_diam() mid() rad() volume() perimeter() is_flat()`,
  `extr_diam_index(b)`, `sort_indices(b,tab)`, `inflate(eps)`, `distance`.
- **Bisection:** `bisect(i, ratio=0.5)` → two sub-boxes (the operation a
  [bisector](../strategy/Bisectors.md) wraps; dReal branches itself).
- **Set ops:** `& | &= |=`, `is_subset`, `intersects`, `is_disjoint`,
  `complementary` (→ union of boxes), `cart_prod`, `random()`.

The `extr_diam_index` / `max_diam` accessors are the primitives a
largest-first/smear bisector uses — and the same quantities dReal inspects when
choosing where to branch.

Related: [`Interval.md`](Interval.md),
[interval chapter](../../chapters/interval.md).
