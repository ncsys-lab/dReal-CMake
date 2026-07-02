# `CtcPolytopeHull` — LP-relaxation hull contractor (opt-in `--polytope` in dReal)

Header:
[`ibex_CtcPolytopeHull.h`](../../../../ibex-fork/src/contractor/ibex_CtcPolytopeHull.h)
(`contractor/`). `class CtcPolytopeHull : public Ctc`. Linearizes a system into a
polytope and runs **2n LP solves** (min & max each variable) to contract to the
relaxation's hull. Header warning: *"can only be used if ibex is installed with a
LP solver (`-DLP_LIB`)."*

> **dReal status (corrected 2026-07-02):** **live opt-in.** `--polytope` is **off by
> default** but functional when set — since commit `fa3b74bd7` dReal builds IBEX with
> **`-DLP_LIB=soplex`** (`CMakeLists.txt:204`, `libsoplex.a` linked), so the 2n LP solves
> run. Constructed at `generic_contractor_generator.cc:61-122` (`dreal_main.cc:169,472`).
> Perf is unmeasured on the main NRA path; `--forall-polytope` (the ∃∀ variant) measured
> *mixed* (`exists_forall_perf.md:195-197`). *(Earlier text here said "dormant / `LP_LIB=none`
> / non-functional if forced" — that was pre-`fa3b74bd7` and is wrong now; see
> [`README.md`](../../README.md) top banner.)*

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
