# `CtcLinearRelax` — turnkey X-Taylor LP relaxation (dormant in dReal)

Header:
[`ibex_CtcLinearRelax.h`](../../../../ibex-fork/src/contractor/ibex_CtcLinearRelax.h)
(`contractor/`). `class CtcLinearRelax : public CtcPolytopeHull`. A convenience
subclass of [`CtcPolytopeHull`](./CtcPolytopeHull.md) that builds its own
`LinearizerXTaylor` over an `ExtendedSystem` — i.e. the polytope-hull contractor
pre-wired with the X-Taylor relaxation, no `Linearizer` to pass.

> **dReal status:** **not used / dormant.** Same blocker as `CtcPolytopeHull`:
> dReal builds IBEX with `-DLP_LIB=none`, so the `2n` Simplex calls have no LP
> backend. See [`../../dreal-ibex-usage.md`](../../dreal-ibex-usage.md).

## Constructor (verbatim)

```cpp
CtcLinearRelax(const ExtendedSystem& sys);
```

The *only* argument is an `ExtendedSystem`. Internally (`ibex_CtcLinearRelax.cpp`):

```cpp
CtcLinearRelax::CtcLinearRelax(const ExtendedSystem& sys)
  : CtcPolytopeHull(*new LinearizerXTaylor(sys)), sys(sys) { }
```

So it inherits the [`CtcPolytopeHull`](./CtcPolytopeHull.md) LP defaults
(`max_iter=100`, `time_out=100`s, `eps=1e-9`) and a **default-configured**
`LinearizerXTaylor` — no corner-policy / slope-formula choice is exposed at this
constructor (cf. the dormant-polytope path, which *does* pick
`RELAX, RANDOM_OPP, HANSEN`). `contract` just calls `CtcPolytopeHull::contract`;
the commented-out `BxpLinearRelaxArgMin` line-search is disabled in the `.cpp`
(*"seems not interesting"*).

## Relationship to the audit

This is essentially the "easy button" for the X-Taylor polytope path. Whether to
use it is the **same** tier-D decision as reviving `CtcPolytopeHull`: re-add an LP
solver dependency (`LP_LIB`→Soplex/CLP) the team deliberately dropped, then
benchmark. It buys *less tunability* than constructing `CtcPolytopeHull` +
`LinearizerXTaylor` by hand, so if the polytope path is revived, prefer the
explicit construction (where corner/slope knobs are reachable) over `CtcLinearRelax`.

Related: [`CtcPolytopeHull.md`](./CtcPolytopeHull.md),
[`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md),
[`../../AUDIT.md`](../../AUDIT.md) D2.
