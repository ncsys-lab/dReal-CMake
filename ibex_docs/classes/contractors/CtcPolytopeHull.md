# `CtcPolytopeHull` — LP-relaxation hull contractor (dormant in dReal)

Header:
[`ibex_CtcPolytopeHull.h`](../../../../ibex-fork/src/contractor/ibex_CtcPolytopeHull.h)
(`contractor/`). `class CtcPolytopeHull : public Ctc`. Linearizes a system into a
polytope and runs **2n LP solves** (min & max each variable) to contract to the
relaxation's hull. Header warning: *"can only be used if ibex is installed with a
LP solver (`-DLP_LIB`)."*

> **dReal status:** **present but dormant.** `--polytope` is **off by default**
> and dReal builds IBEX with `-DLP_LIB=none` — so this contractor is effectively
> inactive (and likely non-functional if forced on, lacking an LP backend). See
> [`../../dreal-ibex-usage.md`](../../dreal-ibex-usage.md). Reviving it is an
> audit-D question.

## Constructors (verbatim)

```cpp
CtcPolytopeHull(Linearizer& lr, int max_iter=LPSolver::default_max_iter,
        int time_out=LPSolver::default_timeout, double eps=LPSolver::default_tolerance);
CtcPolytopeHull(const Matrix& A, const Vector& b, int max_iter=..., int time_out=..., double eps=...);
```

dReal uses the first form with a `LinearizerXTaylor` (`contractor_ibex_polytope.cc:108`):
`LinearizerXTaylor(system, RELAX, RANDOM_OPP, HANSEN)`.

| Param | Default (per header) | Meaning |
|---|---|---|
| `lr` | — | the linearization technique ([`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md)) |
| `max_iter` | `LPSolver::default_max_iter` = **100** | max LP solver iterations |
| `time_out` | `LPSolver::default_timeout` = **100** (s) | per-iteration LP timeout |
| `eps` | `LPSolver::default_tolerance` = **1e-9** | LP resolution accuracy. ⚠️ the `CtcPolytopeHull.h` doc-comment says 1e-10 — stale; the `LPSolver` constexpr (1e-9) is ground truth |

`set_contracted_vars(BitSet)` restricts which variables get the 2 LP solves
(e.g. only the objective in an extended system). Variable/bound order is chosen by
an Achterberg heuristic.

## Audit angle

Two coupled questions, both tier D:
1. **Build:** is the LP relaxation worth re-adding an LP solver dependency
   (Soplex/CLP) that the team deliberately dropped (`LP_LIB=none`)?
2. **Tuning (if revived):** the X-Taylor knobs (corner policy, slope formula) are
   currently fixed at library defaults — see
   [`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md).

Related: [contractor chapter](../../chapters/contractor.md),
[`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md).
