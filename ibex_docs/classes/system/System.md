# `System` (+ `NumConstraint`) — a constraint set with one shared arg list

Header: [`ibex_System.h`](../../../../ibex-fork/src/system/ibex_System.h)
(`system/`). A set of [`NumConstraint`](#numconstraint)s sharing one argument
list, optionally a goal + initial box. Many IBEX algorithms *require* a `System`
(not a loose constraint array) — including [`CtcAcid`](../contractors/CtcAcid.md)
(variable ordering) and [`LinearizerXTaylor`](../linear/LinearizerXTaylor.md).

> **dReal status:** built via [`SystemFactory`](SystemFactory.md) only for the
> (dormant) polytope path. **Re-usable as the `System` `CtcAcid` needs** — the
> main reason this class matters to audit A.

## Fields (verbatim from the [system chapter](../../chapters/system.md))

`const int nb_var`, `const int nb_ctr`, `Function* goal` (NULL if none),
`Function f` (vector-valued constraint fn; rhs → 0, sign kept), `IntervalVector
box` (initial domain), `Array<NumConstraint> ctrs`.

## Subclasses (optimizer-only — dReal ignores)

`NormalizedSystem` (all `g(x)≤0`, optional ε-thickening), `ExtendedSystem` (goal →
constraint + goal var), KKT generation (n+M+R+K+1 vars). These serve the IBEX
`Optimizer`, which dReal doesn't run.

## `NumConstraint`

Header: [`ibex_NumConstraint.h`](../../../../ibex-fork/src/function/ibex_NumConstraint.h)
(`function/` module, not `system/`).
`Function& f` + `CmpOp op` ∈ {`LT(<)`, `LEQ(≤)`, `EQ(=)`, `GEQ(≥)`, `GT(>)`}.
Inequalities must be scalar-valued. dReal feeds these into `SystemFactory::add_ctr`.

Related: [system chapter](../../chapters/system.md),
[constraint chapter](../../chapters/constraint.md),
[`SystemFactory.md`](SystemFactory.md).
