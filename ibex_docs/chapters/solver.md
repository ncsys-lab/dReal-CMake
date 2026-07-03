# Chapter: Solver (`ibexsolve`)

Source: [`solver.rst.txt`](../../../ibex-docs/_sources/solver.rst.txt) ·
`solver.html`. The IBEX `Solver` — a generic interval branch-and-prune that
produces a [COV](strategy.md) covering of a system's solution set, composed from a
contractor + a [bisector](strategy.md) + a cell buffer + precision/timeout/
cell-limit stopping criteria. Driver: the `ibexsolve` executable.

> **dReal status:** **not used** (correctly). dReal runs its *own* branch-and-prune
> as the theory engine inside DPLL(T) — contractors + bisection + the Box
> abstraction + SMT bookkeeping (explanations, lemmas) that the standalone IBEX
> `Solver` doesn't model. See [`../dreal-ibex-usage.md`](../dreal-ibex-usage.md),
> [`../AUDIT.md`](../AUDIT.md) E. The *programming* API is in
> [solver-prog](solver-prog.md).

It is still a useful design reference: IBEX's `Solver` is the canonical example of
how the atomic contractors in [contractor](contractor.md) (HC4, ACID, polytope,
Newton) are *composed* into a default strategy — the same composition dReal does by
hand, and the source of the "ACID-on-by-default" convention behind audit A.
