# Concept: Differential inclusions

## What it is

A **differential inclusion** ẋ ∈ F(x) generalizes an ODE to a *set-valued* RHS: at each x the
derivative may be any element of the set F(x). It is the natural rigorous model for a dynamics
with bounded uncertainty — disturbances, unknown-but-bounded parameters, control inputs in a
range. A solution is any absolutely-continuous x(t) with ẋ(t) ∈ F(x(t)) a.e.; the rigorous
integrator encloses **all** such solutions over a time step.

## How CAPD represents it (confirmed in `diffIncl/MultiMap.h`)

CAPD splits the set-valued field into a **selected ODE plus a perturbation**:

```
f(x, e) = f(x) + g(x, ε)
```

with ε an interval set, the nominal value e0 ∈ ε, and the constraint **g(x, e0) = 0** (the
perturbation vanishes at the nominal field). This is the `MultiMap<FMapT, GMapT>` type:
`m_f` = selection, `m_g` = perturbations; `operator()(X)` returns `f(X) + g(X)`. The decomposed
form lets the integrator estimate the perturbation's contribution tightly instead of treating
the whole field as one wide interval map.

Two integration methods (both `DiffInclusion` subclasses):
- **`DiffInclusionLN`** — Lohner / logarithmic-norm: bounds the perturbation's growth via a
  logarithmic norm of the variational equation.
- **`DiffInclusionCW`** — component-wise estimates.

State carried in `InclRect2Set` (a doubleton set, `C0DoubletonSet` subclass). One step:
`diffIncl(set)` or `diffIncl(set, result)`.

## dReal status

**Unused.** dReal binds uncertain flow vars as CAPD **parameters** over a box and integrates
the frozen-parameter ODE (`dreal-capd-usage.md` §1).

## Why it might matter — and the caveat

For an ODE with a genuinely uncertain bounded parameter, the inclusion model can be tighter
than parameter-over-a-box: a constant interval parameter carried through every Taylor
coefficient compounds the wrapping effect, while the LN/CW methods bound the perturbation's
*accumulation* rigorously. **Caveat (structural):** the uncertainty must be cast as an additive
g(x,ε) with g(x,e0)=0. Parameters entering the field nonlinearly need a hand-built perturbation
map (e.g. for ẋ=p·x, p∈[p0±δ], set g=(p−p0)·x). Whether the tightness gain beats this
per-constraint construction cost on dReal's actual benchmarks is **unmeasured** — no head-to-head
exists.

## Source

[`../../../CAPD/docs/html/group__diffIncl.html`](../../../CAPD/docs/html/group__diffIncl.html)
