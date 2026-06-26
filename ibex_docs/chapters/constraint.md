# Chapter: Constraints

Source: [`constraint.rst.txt`](../../../ibex-docs/_sources/constraint.rst.txt) ·
`constraint.html`. A *numerical constraint* is `f(x) op 0` — a `Function` plus a
comparison operator.

> **dReal status:** uses `NumConstraint` to feed `SystemFactory`
> (`contractor_ibex_polytope.cc`). The docs' own point — *"constraints do not play
> an important role; IBEX is a **contractor** programming library"* — matches dReal:
> it builds contractors, not constraints, for the HC4 path.

## `NumConstraint`

```cpp
Function& f;   // all info lives in the function…
CmpOp op;      // …except the comparison operator
```
`CmpOp` enum: `LT (<)`, `LEQ (≤)`, `EQ (=)`, `GEQ (≥)`, `GT (>)`.

Build: `NumConstraint c(f, LEQ);` (references an external `f`) or
`NumConstraint c(x, x+1<=0);` (owns `c.f`). Up to 6 inline vars; more via
`Array<const ExprSymbol>`.

**Restriction:** inequalities must be scalar-valued (no componentwise
vector `f(x)≤0`).

> Strict vs non-strict (`LT`/`GT` vs `LEQ`/`GEQ`) is a known soundness-sensitive
> spot in dReal's own filter logic (`docs` — `filter_assertion` `nextafter`
> history); IBEX keeps the sign through `System::f` but `NormalizedSystem`
> *weakens* `<` to `≤` ("a little loss of precision" — completeness, not
> soundness). dReal builds its own atoms, so this is informational.

Class detail: [`../classes/system/System.md`](../classes/system/System.md)
(NumConstraint lives in the system module). Related: [function](function.md),
[system](system.md).
