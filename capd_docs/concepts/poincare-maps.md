# Concept: Poincaré maps and time maps

## What it is

Two distinct "advance the flow" tools in `capd::poincare`, easy to conflate but opposite in
which quantity is pinned:

- **TimeMap** — `P(t, x) = φ(t, x)`: transport a set to a **given time** t. Time in, state out.
  This is what dReal uses (`capd::ITimeMap`).
- **PoincareMap** — `P(x) = φ(T(x), x)` where T(x) is the **first return time** to a section
  S = {α(x)=0}. The crossing time is the *unknown the method solves for*; the input is only the
  starting set and the section. Section out (state on S), return time also returned as output.

A **section** is a scalar function α: ℝⁿ → ℝ of **state only** (confirmed:
`AbstractSection::operator()(const VectorType& v)` — no time argument). S = α⁻¹(0). Crossing
direction (`PlusMinus`/`Both`/`MinusPlus`) picks the sign of α̇ at crossing.

## How the rigorous return works (from `poincare_rigorous.html`)

```cpp
IOdeSolver solver(vf, order);
ICoordinateSection section(4, 1);   // section y = 0 (coord 1 of 4)
IPoincareMap pm(solver, section);
C0Rect2Set set(x0);                 // doubleton set on/near the section
interval returnTime;                // NOT initialized by caller
IVector Px = pm(set, returnTime);   // Px = state on section; returnTime = enclosure of T(x)
```

Guards: `setMaxReturnTime` (throws if the section isn't reached in [0, maxReturnTime] — e.g.
trajectory captured by a sink), `setBlowUpMaxNorm` (throws on escape-to-infinity). With C1/C2/Cn
sets, `operator()` additionally returns the monodromy matrix; `computeDP` recomputes it into the
derivative of the Poincaré map dP (all confirmed in the rigorous narrative + `PoincareMap.h`).

## dReal status

`TimeMap`: **used**. `PoincareMap` + sections: **unused** (`dreal-capd-usage.md`).

## Why it might matter — and why it doesn't fit pinned-time gating

The audit asked whether a "time section" could give a tighter pinned-endpoint enclosure than
tube-slicing. **No such construct exists.** A pinned terminal time is *not* a degenerate
time-section: PoincareMap's entire job is to discover an unknown T(x) by Newton-solving
α(φ(t,x))=0, whereas dReal already *has* the terminal time and wants φ at exactly that t —
which is `TimeMap(t, x)`, already in use. Poincaré maps fit a different problem shape: a
**state-defined event** (first crossing of x₃=0). dReal's terminal gate and `forall_t`
invariant are posed over a fixed/bounded time window, not a state event. (One could encode a
time gate as a section by augmenting the state with a clock variable τ̇=1 and section τ−t*=0 —
but that just reproduces TimeMap's transport-to-t with extra dimensions and a Newton solve;
no tightness or speed win is documented or evident.)

## Source

[`../../../CAPD/docs/html/poincare_rigorous.html`](../../../CAPD/docs/html/poincare_rigorous.html)
