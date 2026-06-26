# Chapter: Sets (pavings)

Source: [`set.rst.txt`](../../../ibex-docs/_sources/set.rst.txt) · `set.html`.
A `Set` is a **paving**: a binary tree of boxes, each leaf tagged `YES` (inside),
`NO` (outside) or `MAYBE` (boundary) — a `BoolInterval` status. (Docs note this
module is "recent and under active development".)

> **dReal status:** unused (correctly). dReal produces a model/verdict, not a
> paving of `R^n`. Same **capability-extension** note as [separators](separator.md):
> if dReal wants set output, this is the structure. See
> [`../AUDIT.md`](../AUDIT.md).

## What it offers

- **Creation** — from `R^n`, a box, or a constraint/system (built recursively by
  applying the constraint's fwd-bwd **separator** to a precision parameter).
- **Exploration** — `SetVisitor` (visitor pattern): implement `visit_leaf(box,
  status)` (and optionally `visit_node`). Used for listing/plotting (Vibes).
- **File I/O** — save/load a paving.
- **Algebra** — explicit `Set & Set` (intersection, `&=`), union; or *implicit*
  intersection by contracting a set with a contractor/separator (SIVIA-style),
  recursing to a boundary-box precision. (Docs: building one set from the
  conjunction is more efficient than intersecting two sets.)
- **`SetInterval`** (i-set, Jaulin 2012) — a pair `[S₁,S₂]` with `S₁⊆S⊆S₂`
  bounding an unknown set; the inner/outer status of a separator can be relaxed
  to `MAYBE` to represent one-sided i-sets.

The graphical tool **Vibes** (`#include "vibes.cpp"`, client/server) is used in
all the examples; not relevant to dReal.

Related: [separator](separator.md), [contractor](contractor.md).
