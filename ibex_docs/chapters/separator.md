# Chapter: Separators

Source: [`separator.rst.txt`](../../../ibex-docs/_sources/separator.rst.txt) ·
`separator.html`. A **separator** is a pair of complementary contractors that
splits a box into "provably inside the set S" and "provably outside S" parts
(Jaulin & Desrochers 2014).

> **dReal status:** unused — dReal refutes/satisfies a formula; it does not
> *characterize a set*. Listed here for the **capability-extension** angle: if
> dReal ever wants to *output the feasible set* of an ∃∀ / parametric formula
> (not just sat/unsat), separators + [sets](set.md) are IBEX's machinery for it.
> See [`../AUDIT.md`](../AUDIT.md) "capability extensions".

## What a separator does

For a set S and box `[x]`, `separate(x_in, x_out)` returns `[x_in]`, `[x_out]` with
`([x]∖[x_in])⊂S` and `([x]∖[x_out])∩S=∅`. So the inner contractor carves away the
interior, the outer carves away the exterior; what both leave is the boundary.

## Building separators

- **`SepFwdBwd(f, op)`** — from an inequality `f(x)<0`: outer = fwd-bwd on `f<0`,
  inner = fwd-bwd on `f≥0`. The natural one.
- **`SepCtcPair(c_in, c_out)`** — from two complementary contractors.
- **`SepBoundaryCtc(ctc, pdc)`** — from a boundary contractor + a predicate
  `Pdc::test(box) → {YES,NO,MAYBE}` (a `BoolInterval`).

## Separator algebra (extends set algebra)

`SepNot` (swap in/out), `SepInter` (S₁∩S₂), `SepUnion` (S₁∪S₂), relaxed
intersection, difference (`S₁∩¬S₂`). Plug-in separators exist for geometry
(`SepPolygon` via winding number, `CtcSegment`) — in the ENSTA-robotics plugin,
not core.

> The separator/contractor *duality* mirrors dReal's own positive/negative
> literal contractors, and the `Pdc` YES/NO/MAYBE is the same three-valued shape
> as dReal's per-box theory verdicts — so this is a useful design analogy even
> though the code isn't reused.

Related: [set](set.md) (pavings built from separators), [contractor](contractor.md)
(`CtcExist`/`CtcForAll` — the quantifier cousins).
