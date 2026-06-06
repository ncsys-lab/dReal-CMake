# Codac v2 — Dynamic contractors (index)

Source: web fetch of https://codac.io/manual/contractors/dynamic/ on 2026-06-05 — page returned 404. The v2 manual's only populated dynamic-contractor page is the CtcLohner page (`v2/ctc_lohner.md`). All other ODE contractor pages from v1 either have no v2 page or are stubs.

## What's actually shipped in v2.0.2 (from local header inventory)

| Header file | Class | Notes |
|---|---|---|
| `codac2_CtcLohner.h` | `CtcLohner`, `LohnerAlgorithm` | Same order-2 Lohner as v1; LohnerAlgorithm is public (we use it for trace viz) |
| `codac2_CtcDeriv.h` | `CtcDeriv` | Same as v1, tube-level only |
| `codac2_CtcEval.h` | `CtcEval` | Same as v1; pinpoints z = x(t) on tubes |
| `codac2_CtcInter.h`, `codac2_CtcUnion.h`, `codac2_CtcFixpoint.h`, `codac2_CtcInverse.h`, `codac2_CtcCartProd.h` | various | Composition / set-theoretic — not ODE-specific |
| `codac-unsupported/codac2_unsupported_empty.h` | (empty stub) | No deprecated v1 contractors hidden here |

## What's missing in v2 (relative to v1)

- `CtcPicard`
- `CtcDelay`
- `CtcLinobs`
- `CtcChain`

The v2 web manual lists Picard / Hermite / DiffInclusion in a "provisional plan" but the corresponding headers are **not present** in v2.0.2.

## Implication

The v1 manual page presents a richer-looking dynamic-contractor catalog, but for forward/backward ODE narrowing on `dx/dt = f(x)` the only choices in either version are **CtcLohner** (the same algorithm) and **CtcPicard** (only in v1). The other v1 contractors solve different problems (delay, linear systems, chain rule) and don't apply to dReal's ODE encoding.
