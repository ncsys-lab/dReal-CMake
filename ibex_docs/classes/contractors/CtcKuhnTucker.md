# `CtcKuhnTucker` / `CtcKuhnTuckerLP` — first-order (KKT) contractors (NLP only)

> ⚠ **Stale `LP_LIB=none` blocker note** (pre-`fa3b74bd7`) — corrected 2026-07-02: IBEX now builds
> `-DLP_LIB=soplex` (`CMakeLists.txt:204`), so the "requires `-DLP_LIB`, which dReal's build does not
> provide" reasoning below no longer holds (this contractor is still not wired into dReal, but the LP
> half of the blocker is gone). See [`README.md`](../../README.md) top banner.

Optimization-only contractors that prune a box using the **Karush-Kuhn-Tucker**
first-order conditions of an NLP. Not refutation contractors — they are a
*capability extension* path (rigorous optimization-modulo-theories), not a
completeness/perf lever for dReal's current search.

> **dReal status (both):** **unused.** dReal solves satisfiability, not NLP; it has
> no `NormalizedSystem`/objective to apply KKT to. Relevant only if dReal wants a
> **sound** global optimization bound where it currently uses nlopt's local one —
> see [`../../AUDIT.md`](../../AUDIT.md) capability-extensions.

## `CtcKuhnTucker` — Newton-based

Header:
[`ibex_CtcKuhnTucker.h`](../../../../ibex-fork/src/contractor/ibex_CtcKuhnTucker.h)
(`contractor/`). `class CtcKuhnTucker : public Ctc`.

```cpp
CtcKuhnTucker(const NormalizedSystem& sys, bool reject_unbounded=true);
```

| Param | Default | Meaning |
|---|---|---|
| `sys` | — | the normalized NLP; `contract` expects an **extended** box (with the goal/multiplier vars), for uniformity with other optim contractors |
| `reject_unbounded` | **`true`** | do nothing on an unbounded box — minima at infinity don't satisfy KKT and could be lost by contraction |

**Header warnings (verbatim intent):** building it is *"costly in both time and
memory"* — it symbolically derives the gradients of all constraints (IBEX has no
automatic Hessian), so *"don't build this contractor on-the-fly"*; and `sys.box`
must be fixed before construction (the bound constraints are set once, not updated).

## `CtcKuhnTuckerLP` — LP-based variant

Header:
[`ibex_CtcKuhnTuckerLP.h`](../../../../ibex-fork/src/contractor/ibex_CtcKuhnTuckerLP.h)
(`contractor/`). `class CtcKuhnTuckerLP : public Ctc`.

```cpp
CtcKuhnTuckerLP(const NormalizedSystem& sys, bool reject_unbounded=true);
```

Same interface and the same costly-build / fixed-box warnings, but it discharges
the KKT system with **linear programming** ([`CtcPolytopeHull`](./CtcPolytopeHull.md))
instead of Newton — the header notes this *"avoids pessimism due to
preconditioning"*. Consequence: it **requires `-DLP_LIB`**, which dReal's build
does not provide (`LP_LIB=none`), so it is doubly out of reach today.

## Audit angle

If dReal ever adds rigorous OMT (sound global optimum bounds), the path is the
IBEX `Optimizer` + `ExtendedSystem` + one of these KKT contractors — see
[`../../chapters/optim.md`](../../chapters/optim.md). That is a **capability
addition**, not a speedup for the existing refutation search, and the LP variant
is additionally gated on reviving an LP backend (the same D2 decision as the
[polytope hull](./CtcPolytopeHull.md)).

Related: [`CtcPolytopeHull.md`](./CtcPolytopeHull.md),
[`../../chapters/optim.md`](../../chapters/optim.md),
[`../../AUDIT.md`](../../AUDIT.md).
