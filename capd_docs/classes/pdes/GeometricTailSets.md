# PDE Sets: C0DoubletonSetGeometricTail · C0HODoubletonSetGeometricTail · C1DoubletonSetGeometricTail

## What they are
The **initial-condition set types** that `PdeSolver` propagates — the PDE analogues of the
`dynset` C0/C1 sets ([dynsets-and-wrapping](../../concepts/dynsets-and-wrapping.md)). Each
carries the **leading-mode block as an ordinary `dynset` doubleton** `x + C·r0 + B·r` (so it
inherits CAPD's wrapping-effect machinery — frames, QR, reorganization) **plus a geometric tail**
for the infinite remainder (a `GeometricBound`, stored as `m_currentSeries`). They are
distinguished by the base set they extend:

| Type | Base set (leading block) | Adds |
|---|---|---|
| `C0DoubletonSetGeometricTail<BaseT>` | `dynset::C0DoubletonSet` (default `C0Rect2Policies`) | C^0 value + geometric tail |
| `C0HODoubletonSetGeometricTail<BaseSetT>` | `dynset::C0HOSet<BaseSetT>` (Hermite–Obreshkov, tighter time remainder) | C^0 value + tail, HO step |
| `C1DoubletonSetGeometricTail<Policies>` | `C0DoubletonSetGeometricTail<C1DoubletonSet<…,Policies>>` | variational (Jacobian) blocks `m_dyxId`, `m_dyx` |

(Inheritance and members confirmed in `C0DoubletonSetGeometricTail.h`,
`C0HODoubletonSetGeometricTail.h`, `C1DoubletonSetGeometricTail.h`.)

## Key API (confirmed in headers)
- Constructors mirror the `dynset` doubletons: `(x)`, `(x, r0)`, `(x, C, r0)`,
  `(x, C, r0, B, r)`, `(x, baseSet)` — where `x` is a tail-bound series; the leading block is
  seeded from `x.projection(...)` and `m_currentSeries` accumulates `C·r0 + B·r`.
- `void move(DynSysType& dynsys)` and `void move(DynSysType& dynsys, Set& result)` — one rigorous
  step (`DynSysType = PdeSolver<GeometricBound<interval>>`). The C0 `move` calls
  `dynsys.encloseC0Map(...)` then the inherited `dynset::C0DoubletonSet::move` for wrapping
  control, then `finalizeMove` (which calls `reorganizeIfNeeded` and intersects series vs. set).
  `C1DoubletonSetGeometricTail::move` additionally drives the variational blocks.
- `initMove` / `finalizeMove` — the split-and-recombine bookkeeping (confirmed
  `C0DoubletonSetGeometricTail.h`).
- `evalAt(functional)`, `affineTransformation(A,c)`, `evalAffineFunctional(gradient,x0)` —
  evaluate functionals/sections on the set (used by the PDE Poincaré sections).
- `getCurrentSeries()`, `getLastEnclosure()`/`setLastEnclosure()`, `name()`, conversions to the
  series `VectorType` and to a finite `FiniteVectorType`.

> Note: `initMove`/`finalizeMove` in `C0DoubletonSetGeometricTail.h` call `exit(0)` on an
> internal intersection inconsistency (a "report this to CAPD developers" path) rather than
> throwing — relevant if dReal ever embeds this, since dReal's ODE path relies on catching CAPD
> exceptions.

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** No `src/` reference.

## Why it might matter
These are the PDE counterpart of the `--ode-c0-set` choice (`C0Rect2Set`/`C0HORect2Set`/…). A
dReal PDE contractor would pick one of these as the propagated tube state: `C0…` for value-only
`forall_t` enclosures, `C1…` if variational (backward-narrowing/interval-Newton) sharpening is
wanted — exactly the C0-vs-C1 tradeoff the ODE audit flags
([dreal-capd-usage.md](../../dreal-capd-usage.md)). Because the leading block is a standard
doubleton, the whole wrapping-effect tuning story
([dynsets-and-wrapping](../../concepts/dynsets-and-wrapping.md)) carries over unchanged.

## Source
[classcapd_1_1pdes_1_1C0DoubletonSetGeometricTail.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1C0DoubletonSetGeometricTail.html) ·
[classcapd_1_1pdes_1_1C0HODoubletonSetGeometricTail.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1C0HODoubletonSetGeometricTail.html) ·
[classcapd_1_1pdes_1_1C1DoubletonSetGeometricTail.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1C1DoubletonSetGeometricTail.html)
(headers: `capd/pdes/C{0,0HO,1}DoubletonSetGeometricTail.h`)
