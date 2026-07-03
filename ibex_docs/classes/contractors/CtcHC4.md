# `CtcHC4` — HC4 propagation (dReal hand-rolls the analog)

Header: [`ibex_CtcHC4.h`](../../../../ibex-fork/src/contractor/ibex_CtcHC4.h)
(`contractor/`). `class CtcHC4 : public CtcPropag`. The textbook **HC4**: build a
forward-backward ([`CtcFwdBwd`](./CtcFwdBwd.md)) contractor per constraint of a
CSP/System, then propagate them to a fixpoint with [`CtcPropag`](./Operators.md).

> **dReal status:** **not used.** dReal runs the *same algorithm* but through its
> own agenda/fixpoint (`contractor_worklist_fixpoint.cc`) over per-constraint
> callback-bearing fwd-bwd contractors, because it needs SMT bookkeeping
> (explanations, theory lemmas, the Box abstraction) that the stock strategy
> class doesn't model. See [`../../dreal-ibex-usage.md`](../../dreal-ibex-usage.md).

## Constructors (verbatim)

```cpp
CtcHC4(const Array<NumConstraint>& csp, double ratio=default_ratio, bool incremental=false);
CtcHC4(const System& sys,              double ratio=default_ratio, bool incremental=false);
```

`default_ratio` is **inherited from `CtcPropag` = 0.01** (not redefined here).
Note the `CtcPropag` member-comment drift: it reads "set to 0.1", but the
`static constexpr default_ratio = 0.01` is ground truth — see
[`Operators.md`](./Operators.md) and [COMPARISON](./COMPARISON.md).

| Param | Default | Meaning |
|---|---|---|
| `csp` / `sys` | — | the constraint set; one `CtcFwdBwd` is built per constraint |
| `ratio` | `CtcPropag::default_ratio` = **0.01** | a projection removing `< ratio·diam` of a domain is not re-propagated |
| `incremental` | `false` | start propagation from impacted vars only (when called with an impact mask) |

## Why it matters for the audit

`CtcHC4` is the *reference shape* for what dReal's worklist already does — so it is
**not** a porting target. The leverage is not "use IBEX's HC4" but "layer a
shaving contractor ([`CtcAcid`](./CtcAcid.md) / [`Ctc3BCid`](./Ctc3BCid.md)) on top
of dReal's existing HC4 loop." The one tunable HC4 shares with dReal is the
propagation stop-`ratio` — the B1 cross-check in [`../../AUDIT.md`](../../AUDIT.md).

Related: [`Operators.md`](./Operators.md) (the `CtcPropag` base),
[`CtcFwdBwd.md`](./CtcFwdBwd.md) (the per-constraint sub-contractor),
[contractor chapter](../../chapters/contractor.md).
