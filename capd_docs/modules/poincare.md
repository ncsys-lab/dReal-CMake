# Module: Poincare (`group__poincare`)

## What it is

CAPD's `capd::poincare` namespace: rigorous and non-rigorous **Poincaré return maps**,
**time maps**, and the **section** type family they cross. A Poincaré map sends a point on a
section S = {α(x)=0} to its first return to S under the flow; the crossing time is *solved
for*, not specified. The `TimeMap` class in this same module is the orthogonal tool —
transport a set to a *given* time — and is the one dReal actually uses.

## Contents (from extracted module page)

- Maps: `PoincareMap` (rigorous), `BasicPoincareMap` (non-rigorous base), `TimeMap`.
- Sections (all scalar functions α: ℝⁿ→ℝ of **state only**): `AbstractSection`,
  `CoordinateSection` (xᵢ=c), `AffineSection` (a·x=c), `NonlinearSection` (α from a parsed
  expression string).
- Support: `PoincareException`, `SaveStepControl`, `SectionDerivativesEnclosure`,
  `CrossingDirection` enum (`PlusMinus`, `Both`, `MinusPlus`).

Note: several section pages carry a copy-pasted docstring "TimeMap class provides class that
serves as Poincare section…" — a Doxygen comment error, not a claim that sections involve
time. The signatures (`operator()(const VectorType& v)`) confirm sections are state functions.

## dReal status

`TimeMap` is **used** (as `capd::ITimeMap`, per `dreal-capd-usage.md` §2-3). `PoincareMap`
and the section family are **unused** — `dreal-capd-usage.md` lists "PoincareMap / sections"
under "Conspicuously NOT used", hypothesizing a pinned terminal time is a degenerate
time-section. That hypothesis does not hold (see below).

## Why it might matter

The audit hypothesis: replace tube-slice terminal gating with a single tight pinned-endpoint
enclosure from a "time section". **Finding: there is no time section.** Sections are α(x)=0
over state; PoincareMap *solves for* an unknown crossing time (opposite of pinning t). dReal
already has the right tool for a fixed terminal time — `TimeMap` transports to a *given*
`time`. A Poincaré map would only help a different problem: a state-defined event (e.g.
"first time x₃=0"), which is not how dReal's `forall_t` / terminal gate is posed.

## Source

[`../../../CAPD/docs/html/group__poincare.html`](../../../CAPD/docs/html/group__poincare.html)
