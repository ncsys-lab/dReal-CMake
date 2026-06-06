# Codac v1 — CtcDeriv

Source: https://codac.io/v1/manual/05-dynamic-contractors/01-ctc-deriv.html (fetched 2026-06-05)

## Constraint

ẋ(t) = v(t), binding a state trajectory to its derivative trajectory.

## Usage

```cpp
ctc::deriv.contract(x, v);                                 // FWD + BWD
ctc::deriv.contract(x, v, TimePropag::BACKWARD);           // BWD only
```

## Prerequisites

`x` and `v` must share:
- Same slicing (time sampling)
- Same t-domain [t₀, tf]
- Same dimension (vector case)

## What it contracts

Only `x` is contracted: "it can be theoretically proved that [v](·) cannot be contracted when [x](·) is not a degenerate tube."

## Why this doesn't help dReal's ODE workload

`CtcDeriv` needs an **explicit derivative tube `v`** — it doesn't compute `v = f(x)` itself. To use it for `ẋ = f(x)` we would have to:
1. Build a tube `v` by evaluating `f` over the current `x` enclosure at every slice
2. Pass `(x, v)` to `CtcDeriv`

That's just a hand-rolled Picard iteration with extra steps. Lohner already does this end-to-end and tightens better.

## Reference

Rohou et al., "Guaranteed computation of robot trajectories," *Robotics and Autonomous Systems* 93:76–84 (2017).
