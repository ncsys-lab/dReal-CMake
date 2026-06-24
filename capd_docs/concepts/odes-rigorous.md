# Concept: ODEs — interval-based (rigorous) methods

## What it is
The narrative for CAPD's rigorous IVP solvers: how a subset of phase space is represented in
memory, and the three strategies for advancing it under a flow. (`odes_rigorous.html`)

## Key points (confirmed in doc)
**Initial-condition representations.** Two rigorous solver families — Taylor and
Hermite-Obreshkov (explicit-implicit). Four recommended set classes:
- `C0Rect2Set` / `C0HORect2Set` — **doubleton** `x + C·r0 + r`, with `x` a point vector, `C`
  an interval matrix near-orthogonal.
- `C0TripletonSet` / `C0HOTripletonSet` — **tripleton** (an extra organized term).
- Rect2 variants use high-order Taylor; HO variants use Taylor as predictor + Hermite-Obreshkov
  implicit corrector. All convert to `IVector` via cast. An affine 3-arg constructor
  `(x, C, r0)` is the recommended way to seed an affine initial set.

**Three strategies:**
1. **One-step** — `IOdeSolver solver(vectorField, order); set.move(solver);`. The solver
   auto-predicts the step (unless turned off) and validates existence of the solution over
   `[0, h]`; **throws if it cannot validate** existence. The doc explicitly notes one-step is
   "not recommended in general" except for very wide initial conditions at low order (3-5).
2. **Long-time** — `ITimeMap` with automatic step control (see
   `timemap-integration.md`). Recommended for long integration.
3. **Solution curve** — a `SolutionCurve` functional object.

## dReal status
**Used (strategy 2).** dReal seeds a C0 set (`--ode-c0-set` ∈
{`C0Rect2Set`,`C0HORect2Set`,`C0TripletonSet`}) and drives `ITimeMap` with
`stopAfterStep(true)`, walking adaptive steps (dreal-capd-usage.md §integration path 2-4).
The throw-on-non-validation is dReal's catch-all → skip-narrowing path (sound).

## Why it might matter
dReal seeds each set from a plain `IVector` (interval box). The **affine** `(x, C, r0)`
constructor would let dReal carry the box's correlation structure into CAPD instead of
re-wrapping it as an axis-aligned interval each ICP call — a wrapping-effect (completeness)
improvement, no soundness change. The `C0TripletonSet`/HO variants are the doc's
"most efficient in typical cases" recommendation; worth confirming dReal's default matches.

## Source
[odes_rigorous.html](../../../CAPD/docs/html/odes_rigorous.html)
