# Codac v1 — `src/core/contractors/dyn/` inventory

Source: https://github.com/codac-team/codac/tree/codac1/src/core/contractors/dyn (read 2026-06-05)

## Files

| Contractor | Header | Source |
|---|---|---|
| Base class | `codac_DynCtc.h` | `codac_DynCtc.cpp` |
| `CtcChain` | `codac_CtcChain.h` | `codac_CtcChain.cpp` |
| `CtcDelay` | `codac_CtcDelay.h` | `codac_CtcDelay.cpp` |
| `CtcDeriv` | `codac_CtcDeriv.h` | `codac_CtcDeriv.cpp` |
| `CtcEval` | `codac_CtcEval.h` | `codac_CtcEval.cpp` |
| `CtcLinobs` | `codac_CtcLinobs.h` | `codac_CtcLinobs.cpp` |
| `CtcLohner` | `codac_CtcLohner.h` | `codac_CtcLohner.cpp` |
| `CtcPicard` | `codac_CtcPicard.h` | `codac_CtcPicard.cpp` |
| `CtcStatic` | `codac_CtcStatic.h` | `codac_CtcStatic.cpp` |

(9 contractor pairs total.)

## API shape

All contractors take an `ibex::Function` (or `codac::TFnc`) and operate on `codac::Tube` / `codac::TubeVector`. Common signature:

```cpp
class CtcXxx : public DynCtc {
  CtcXxx(const Function& f, ...);
  void contract(Tube&,       TimePropag = FORWARD|BACKWARD);
  void contract(TubeVector&, TimePropag = FORWARD|BACKWARD);
  void contract(std::vector<Domain*>&) override;
};
```

## v1 vs v2 API translation cost (for our codebase)

To replace v2 in `contractor_odes_codac.cc`:

| v2 API | v1 API |
|---|---|
| `codac2::AnalyticFunction<VectorType>` | `ibex::Function` (we already build these via `IbexConverter`) |
| `codac2::SlicedTube<IntervalVector>` | `codac::TubeVector` |
| `codac2::create_tdomain(...)` | `codac::TubeVector(t_domain, dt, n)` constructor |
| `tube.set(X0, 0.)` / `tube.last_slice()->codomain()` | `tube.set(X0, t0)` / `tube(t_ub)` |
| `cache->ctc.contract(tube, FWD_BWD)` | `cache->ctc.contract(tube, FORWARD|BACKWARD)` |
| `codac2::LohnerAlgorithm` (for trace) | not exposed — would need to replicate via `CtcLohner` + small step tubes |

Net: a moderate rewrite (~200–300 lines), localized to `contractor_odes_codac.cc` and the cache type. The dReal-side contractor (`contractor_odes.cc`) shape doesn't change.

A non-trivial concern: the `LohnerAlgorithm`-based trace path we use for `--visualize` would need re-engineering since v1 doesn't expose the same per-step low-level API.
