# `CtcFwdBwd` — forward-backward / HC4Revise (dReal's contraction, reimplemented)

Header: [`ibex_CtcFwdBwd.h`](../../../../ibex-fork/src/contractor/ibex_CtcFwdBwd.h)
(`contractor/`). `class CtcFwdBwd : public Ctc`. The stock wrapper that turns one
constraint into a forward-backward (HC4Revise) contractor.

> **dReal status:** the *algorithm* is dReal's hot loop, but dReal does **not**
> instantiate this class — it calls `Function::backward(y, box, callback)` from
> its own `ContractorIbexFwdbwd` wrapper so it can pass the per-variable
> **callback** (fork patches #2/#5/#6/#7) that `CtcFwdBwd` doesn't expose. So
> `CtcFwdBwd` is the reference for *what* dReal computes, not the code path it
> runs.

## Constructors (verbatim)

```cpp
CtcFwdBwd(const Function& f, CmpOp op=EQ);          // f(x)=0 (default) or f(x)<=0, …
CtcFwdBwd(const Function& f, const Domain& y);       // f(x) in [y]
CtcFwdBwd(const Function& f, const Interval& y);
CtcFwdBwd(const Function& f, const IntervalVector& y);
CtcFwdBwd(const Function& f, const IntervalMatrix& y);
CtcFwdBwd(const NumConstraint& ctr);                 // ctr kept by reference
CtcFwdBwd(const System& sys, int i);                 // ith constraint — uses system cache
```

The `(System, i)` form *"benefits from the system cache"* — the same forward-eval
memoization the [Box Properties](../../chapters/strategy.md) `Bxp` mechanism
generalizes; dReal does its own caching instead.

## Why it matters for the audit

`CtcFwdBwd` is the **sub-contractor** the shaving contractors
([`CtcAcid`](CtcAcid.md), [`Ctc3BCid`](Ctc3BCid.md)) call per slice. When dReal
wires ACID in, the sub-contractor should be dReal's callback-bearing fwd-bwd
wrapper (so lemma tracking survives), not a fresh stock `CtcFwdBwd`.

Related: [function chapter](../../chapters/function.md) (`Function::backward`),
[contractor chapter](../../chapters/contractor.md).
