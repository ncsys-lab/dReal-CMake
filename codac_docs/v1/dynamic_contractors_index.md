# Codac v1 — Dynamic contractors (index)

Source: https://codac.io/v1/manual/05-dynamic-contractors/index.html (fetched 2026-06-05)

## Catalog

Seven dynamic contractors are listed; each operates on `Tube` / `TubeVector` objects in time.

| Contractor | Constraint | Predefined object (C++ / Python) |
|---|---|---|
| `CtcDeriv` | ẋ(t) = v(t) | `ctc::deriv` / `ctc.deriv` |
| `CtcEval` | y_i = x(t_i) | `ctc::eval` / `ctc.eval` |
| `CtcLohner` | ẋ(t) = f(x(t)) — guaranteed integration (Lohner) | — (constructed per-instance) |
| `CtcPicard` | ẋ(t) = f(x(t)) — guaranteed integration (Picard) | — (constructed per-instance) |
| `CtcDelay` | x(t) = y(t+a) | `ctc::delay` / `ctc.delay` |
| `CtcLinobs` | ẋ = Ax + Bu (linear systems) | — (constructed per-instance) |
| `CtcChain` | ẋ₁ = x₂, ẋ₂ = x₃, … (cascaded derivatives) | — |

## What only v1 has (relative to v2.0.2)

- `CtcPicard` (no v2 equivalent)
- `CtcDelay`
- `CtcLinobs`
- `CtcChain`

## What v1 and v2 share

- `CtcDeriv`, `CtcEval`, `CtcLohner` exist in both, with the same constraint semantics. The implementation of Lohner is the same order-2 algorithm in both branches (verified by reading both source files).

## Tube model in v1

All v1 dynamic contractors operate on `codac::Tube` / `codac::TubeVector`. v2 replaced this with `codac2::SlicedTube` + `TDomain`. The API call shapes are different even where the underlying algorithm is identical.
