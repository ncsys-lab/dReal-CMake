# PoincareMap (`capd::poincare::PoincareMap<SolverT, SectionT>`)

## What it is

Rigorous first-return map to a Poincaré section. Template over the ODE solver and the section
type; dReal's typedef would be `capd::IPoincareMap = PoincareMap<IOdeSolver>`. Wraps
`BasicPoincareMap` (non-rigorous) and adds rigorous error estimates. Computes
P(x) = φ(T(x), x) on S = {α(x)=0}, with T(x) the first return time **solved for** internally.

## Key API (from `PoincareMap.h` + extracted class page)

```cpp
PoincareMap(Solver& solver, SectionType& section,
            CrossingDirection direction = Both, const BoundType& errorTolerance = 0.1);

// C0 sets: value + return time (returnTime is an OUTPUT)
VectorType operator()(T& theSet, ScalarType& out_returnTime, int n = 1);   // n-th iterate
VectorType operator()(T& theSet, int n = 1);

// C1 sets: also the monodromy / flow derivative
VectorType operator()(T& theSet, MatrixType& dF, ScalarType& out_returnTime, int n = 1);
MatrixType computeDP(const VectorType& Px, const MatrixType& derivativeOfFlow,
                     ScalarType returnTime = 0);   // monodromy -> dP

void setMaxReturnTime(double);    // throw if section not reached in [0, maxReturnTime]
void setBlowUpMaxNorm(double);    // throw if |trajectory| exceeds threshold (escape)
void setCrossingDirection(CrossingDirection);   // PlusMinus / Both / MinusPlus
```

Return: the state **on the section** (VectorType); the crossing time comes back via the
`out_returnTime` reference, not as the primary return. (All signatures verified in header.)

## dReal status

**Unused.** Listed under "Conspicuously NOT used" in `dreal-capd-usage.md`.

## Why it might matter

The audit hypothesized it could replace tube-slice terminal gating with a tighter pinned
endpoint. It cannot: PoincareMap *finds* an unknown crossing time of a **state** section, the
inverse of dReal's "evaluate φ at a known terminal time t" (that is `TimeMap`, already used).
PoincareMap fits only a **state-event** gate (first time x_i = c), which is not how dReal poses
its terminal/`forall_t` constraints. Secondary value: its C1 path yields the monodromy matrix /
dP — a sharper tool for X₀ backward narrowing — but that benefit is about variational
(C1) integration, available via the solver/TimeMap C1 path too, and is independent of the
Poincaré-map framing.

## Source

[`../../../../CAPD/docs/html/classcapd_1_1poincare_1_1PoincareMap.html`](../../../../CAPD/docs/html/classcapd_1_1poincare_1_1PoincareMap.html)
