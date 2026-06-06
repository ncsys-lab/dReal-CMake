# Codac v2 — CtcLohner

Sources:
- https://codac.io/manual/contractors/dynamic/ctclohner.html (fetched 2026-06-05; web docs are sparse)
- `gcc_build/codac-install/include/codac-core/codac2_CtcLohner.h` (verbatim, 2026-06-05)

## Header (the authoritative API source)

```cpp
struct GlobalEnclosureError : public std::runtime_error { ... };

class LohnerAlgorithm {
public:
  constexpr static const double FWD = 1., BWD = -1.;

  LohnerAlgorithm(const AnalyticFunction<VectorType>* f,
                  double h,
                  bool forward,
                  const IntervalVector& u0,
                  int contractions = 1,
                  double eps = 0.1);

  const IntervalVector& integrate(unsigned int steps, double H = -1);
  void contractStep(const IntervalVector& x);
  const IntervalVector& getLocalEnclosure() const;
  const IntervalVector& getGlobalEnclosure() const;

private:
  unsigned int   _dim;
  double         _h;
  double         _direction;
  double         _eps;
  int            _contractions;
  IntervalVector _u;          //!< local enclosure
  IntervalVector _z;          //!< Taylor-Lagrange remainder (order 2)
  IntervalVector _r;
  IntervalVector _u_tilde;    //!< global enclosure
  Matrix         _B, _Binv;
  Vector         _u_hat;
  const AnalyticFunction<VectorType>* _f;
};

class CtcLohner {
public:
  CtcLohner(const AnalyticFunction<VectorType>& f, int contractions = 5, double eps = 0.1);
  void contract(SlicedTube<IntervalVector>& tube, TimePropag t_propa = TimePropag::FWD_BWD) const;
protected:
  AnalyticFunction<VectorType> _f;
  int    _contractions;
  int    _dim;
  double _eps;
};
```

## Order and configurability

- **Taylor order 2, hardcoded.** Field comment: `IntervalVector _z; //!< Taylor-Lagrange remainder (order 2)`.
- Only knobs: `contractions`, `eps`. No order parameter, no step adapter.
- Manual page says automatic step adjustment is "not yet implemented" — confirms our manual `n_steps` heuristic is doing work the library should do.

## `LohnerAlgorithm` exposes proper forward/backward integration

Constructor takes `bool forward`. With `forward=false`, the algorithm integrates `dx/dt = f(x)` in reverse time. This is the basis for a backward-direction contractor without the unsafe endpoint swap that `CtcLohner` is stuck with.

## Caveat (from the v2 page verbatim)

> "The contractor might throw a runtime error when it cannot find a global enclosure over a specific time step. This usually happens when the time step is too large."

→ matches `GlobalEnclosureError` we catch in `run_lohner_integration`.

## Cross-reference

`codac.io/manual/contractors/dynamic/` (the parent index for v2 dynamic contractors) is sparsely populated — only CtcLohner has a dedicated page. CtcPicard / CtcHermite / CtcDiffInclusion are listed in the "provisional plan" with no implementation. Confirmed by the local header inventory (`v2/local_header_inventory.md`).
