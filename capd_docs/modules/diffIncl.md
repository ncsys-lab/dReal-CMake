# Module: DiffIncl (`group__diffIncl`)

## What it is

`capd::diffIncl`: rigorous integration of **differential inclusions** ẋ ∈ F(x), where the RHS
is a set-valued map. CAPD models F as a `MultiMap` decomposing the field into a selected ODE
plus a bounded perturbation: `f(x,e) = f(x) + g(x,ε)`, with ε an interval set, e0 ∈ ε, and
**g(x,e0)=0** (confirmed in `diffIncl/MultiMap.h`). The integrator computes an enclosure of
*all* solutions of the inclusion over a step.

## Contents (from extracted module page + headers)

- `DiffInclusion` — abstract base (order, tolerances, step control, `enclosure(t,x)`).
- `DiffInclusionCW` — component-wise perturbation method.
- `DiffInclusionLN` — Lohner / logarithmic-norm method.
- `InclRect2Set` — doubleton set type for inclusion integration (extends `C0DoubletonSet`).
- `MultiMap` — the `f + g` RHS container.

## dReal status

**Unused.** `dreal-capd-usage.md` lists `DiffInclusion` under "Conspicuously NOT used" and
records that dReal currently handles uncertain flow parameters by treating them as CAPD
**parameters** bound over a box (§1: "Non-integrated flow vars are CAPD *parameters*").

## Why it might matter

Audit hypothesis: a DiffInclusion is a better model for ODEs with bounded uncertain parameters
than parameter-over-a-box. **Partly — with a structural caveat.** The MultiMap form requires
uncertainty to be expressible as an **additive perturbation** g(x,ε) that vanishes at the
nominal value. A parameter entering the field *nonlinearly* (e.g. ẋ = p·x with p ∈ [a,b]) does
not fit `f(x)+g(x,ε)` directly; you would hand-construct g = (p−p0)·x as the perturbation map.
Where it *does* fit, the CW/LN methods can give tighter enclosures than freezing the parameter
to an interval and integrating, because they account for the perturbation's accumulation
rigorously rather than carrying a wide constant interval through every Taylor coefficient. This
is a real but non-trivial integration: a separate solver type, a new set type (`InclRect2Set`),
and per-constraint construction of the perturbation map.

## Source

[`../../../CAPD/docs/html/group__diffIncl.html`](../../../CAPD/docs/html/group__diffIncl.html)
