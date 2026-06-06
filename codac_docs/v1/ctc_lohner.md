# Codac v1 — CtcLohner

Sources:
- https://codac.io/v1/manual/05-dynamic-contractors/03-ctc-lohner.html (fetched 2026-06-05)
- `github.com/codac-team/codac/blob/codac1/src/core/contractors/dyn/codac_CtcLohner.{h,cpp}` (read 2026-06-05)

## Constructor

```cpp
explicit CtcLohner(const Function& f, int contractions = 5, double eps = 0.1);
```

`Function` is `ibex::Function`. Defaults match v2.

## Contract methods

```cpp
void contract(codac::TubeVector& tube, TimePropag t_propa = FORWARD | BACKWARD);
void contract(codac::Tube& tube,       TimePropag t_propa = FORWARD | BACKWARD);
void contract(std::vector<codac::Domain*>& v_domains) override;
```

(Scalar `Tube` and `TubeVector` overloads. v2 only has `SlicedTube<IntervalVector>`.)

## Algorithm

Same Lohner-based guaranteed integration as v2:

> "Estimation of global enclosure over an integration step ... then gate estimation using the enclosure and input/output gates, with optional iterations for tighter bounds."

### Taylor order

**Order 2, hardcoded.** From `codac_CtcLohner.cpp`:

```cpp
IntervalVector z; //!< Taylor-Lagrange remainder (order 2)
...
z1 = 0.5 * h * h * f->jacobian(u_t) * f->eval_vector(u_t);
```

The `0.5 * h * h` factor is the order-2 Taylor coefficient `h²/2!`. No runtime/compile-time switch for order.

### Dependencies

Internal includes only: `codac_DynCtc.h`, `codac_TFnc.h`, `codac_Slice.h`. **No CAPD, no FILIB.** Uses IBEX for interval arithmetic and Eigen for QR decomposition.

## Documented caveat

> "The contractor may throw runtime errors when unable to find a global enclosure over a time step, typically when the discretization frequency is too large."

Matches v2's `GlobalEnclosureError`.

## v1 vs v2 — same algorithm

v1 and v2 CtcLohner are the same order-2 Lohner algorithm. The only differences are:
- v1 takes `ibex::Function`; v2 takes `codac2::AnalyticFunction<VectorType>`
- v1 operates on `Tube`/`TubeVector`; v2 on `SlicedTube`
- v1 is in C++17; v2 requires C++20 (concepts, std::numbers)

Switching to v1 CtcLohner does NOT change the per-step accuracy.
