# Module: Capdlib (`group__capdlib`) — the `I*` typedefs dReal binds

## What it is
The list of concrete `capd::` typedefs instantiated for interval (`I`) arithmetic, defined
via `#include "capd/capdlib.h"`. This is the binding surface: dReal names types from here.

## Key API (confirmed in doc)
Solver typedefs (from `odes_rigorous.html` / `dynsys/typedefs.h`):
- `IOdeSolver`  = `capd::dynsys::OdeSolver<IMap>` — C0 + first-order variational.
- `IC2OdeSolver` = `capd::dynsys::C2OdeSolver<IMap>` — first + second order variational.
- `ICnOdeSolver` = `capd::dynsys::CnOdeSolver<IMap>` — higher-order variational.
- `ITimeMap` / `IC2TimeMap` / `ICnTimeMap` — long-time integrators (matching C-order).

C0 set typedefs (all `< capd::IMatrix, ... Policies >`):
- `C0Rect2Set`, `C0TripletonSet` (Taylor); `C0HORect2Set`, `C0HOTripletonSet` (HO);
  also `C0Intv2Set`, `C0Pped2Set`, `C0RectSet`, `C0PpedSet`, `C0BallSet`, `C0FlowballSet`.
- `DefaultC0Set<MatrixT>` = `C0TripletonSet`.

C1/C2/Cn set typedefs (variational, **confirmed present**): `C1Rect2Set`, `C1Pped2Set`,
`C1HORect2Set`, `C11Rect2Set`; `C2Rect2Set`, `C2Pped2Set`; `CnRect2Set`,
`CnMultiMatrixRect2Set`; plus `DefaultC1Set<MatrixT>` = `C1DoubletonSet<…,C1Rect2Policies>`.

## dReal status
**Partially used.** dReal binds `IMap`, `IOdeSolver`, `ITimeMap`, `interval`, `IVector`,
`IntervalError`, and `C0Rect2Set`/`C0HORect2Set`/`C0TripletonSet` (dreal-capd-usage.md
§Symbols). The `IC2/ICn` solvers, the `IC2/ICn` TimeMaps, the HO C0 tripleton, and the
**entire C1/C2/Cn set family** are listed here but unused.

## Why it might matter
`capdlib.h` shows the variational solvers and sets are ready-made typedefs — adopting C1
narrowing needs no new instantiation, only `IC2OdeSolver`/`C1Rect2Set` + a Jacobian-driven
contraction step. Many alternative C0 sets (`C0Intv2Set`, `C0Pped2Set`, `C0BallSet`) are also
free to swap into `--ode-c0-set` to trade speed against wrapping-effect tightness.

## Source
[group__capdlib.html](../../../CAPD/docs/html/group__capdlib.html)
