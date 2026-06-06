# Codac v1 — CtcPicard

Sources:
- https://codac.io/v1/manual/05-dynamic-contractors/04-ctc-picard.html (web page is incomplete: "The writing of this page is in progress")
- `github.com/codac-team/codac/blob/codac1/src/core/contractors/dyn/codac_CtcPicard.{h,cpp}` (read 2026-06-05)

## Constructor

```cpp
CtcPicard(const Function& f, float delta = 1.1);
CtcPicard(const TFnc& f, float delta = 1.1);
```

Takes `ibex::Function` or `codac::TFnc`. `delta > 1` is the enclosure inflation factor used inside the iteration.

## Contract methods

```cpp
void contract(Tube& x, TimePropag t_propa = FORWARD | BACKWARD);
void contract(TubeVector& x, TimePropag t_propa = FORWARD | BACKWARD);
void contract(std::vector<Domain*>& v_domains);
```

## Algorithm

Picard-iteration enclosure. From `codac_CtcPicard.cpp`:

```cpp
do {
  m_picard_iterations++;
  x_guess = x_enclosure;
  // ... compute x_enclosure ...
} while (!x_enclosure.is_interior_subset(x_guess));
```

**Convergence criterion:** new enclosure must be **strictly inside** the previous guess. If contractive, the iteration converges. If not contractive (e.g. unstable dynamics over the slice), it does not converge — the contractor must abort or fall back.

**Delta:** inflates the guess interval around its midpoint each iteration:

```cpp
x_guess[i] = x_guess[i].mid()
           + delta * (x_guess[i] - x_guess[i].mid())
           + Interval(-EPSILON, EPSILON);
```

Larger `delta` → wider guess → more likely to be a Picard contraction fixed point → fewer iterations but looser final enclosure. Smaller `delta` → tighter but may not converge.

## Per-slice cost model

Two `m_f.eval_vector(...)` calls per slice per Picard iteration. Cheaper per step than Lohner (which also needs a Jacobian), but the iteration count is data-dependent.

## Adaptive slicing note

> "Unbounded slices trigger sampling if diameter exceeds 0.2% of total time domain."

## v1 docs verdict on Picard vs Lohner

From the v1 CtcLohner page:

> "This contractor [Lohner] is supposed to yield better results than [Picard] as long as the tubes are 'thin enough'."

Implication for our use case: ICP narrows tubes aggressively across iterations. By the time the contractor matters, tubes are thin → Lohner is the documented winner. Picard is the wide-tube fallback.

## Dependencies

Internal includes only: `codac_DynCtc.h`, `codac_TFnc.h`, `codac_Slice.h`. No CAPD, no FILIB.
