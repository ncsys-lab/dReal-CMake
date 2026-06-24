# C1HOSet

## What it is
The **C1 higher-order** wrapper, `C1HOSet<BaseSetT>`: it carries the C1 jet (position +
Jacobian `∂φ/∂x₀`) in the geometry inherited from its base set and computes each step's image
**twice** — by the **Taylor** method and the **Hermite–Obreshkov** method — taking the
**intersection** (confirmed `C1HOSet.h` / "evaluation… realized by intersection of two
methods: the Taylor method and the Hermite-Obreshkov method"). The C1 analogue of `C0HOSet`:
HO tightens the per-step *time* remainder, the C1 part gives the sensitivity matrix.

## Key API
- `move(Solver&)` / `move(Solver&, result)` — fills predictor/corrector C1 base-set copies
  and intersects; needs a C1 solver.
- `evalAt(f)` — "intersection of evalAt for two representations of this set: predictor and
  corrector" (the HO tightening, exposed for functionals).
- `operator MatrixType()` — "enclosure of derivative in the canonical coordinates" (the
  Jacobian).
- **Order cap:** Taylor order ≤ 64, ≥ 3 (binomial-coefficient integer capacity, `C1HOSet.h`).

## dReal status
**Not used (structural).** dReal wires the C0 HO set (`C0HORect2Set`) but no C1 set of any
kind. The two C1-HO instantiations `C1HORect2Set = C1HOSet<C1Rect2Set>` and
`C1HOPped2Set = C1HOSet<C1Pped2Set>` exist in `typedefs.h` but are unwired.

## Why it might matter
Only relevant *after* the structural C1 step (`classes/sets/C1DoubletonSet.md`): if dReal
ever consumes the Jacobian for backward narrowing **and** the tube is time-remainder-limited,
`C1HORect2Set` stacks both tightenings. A header caveat to note: the C1-part reorganization
carries a "TODO… valid for doubleton representations of C^1 part, only" (`C1HOSet` doc) — a
maturity flag, not a soundness concern. Until C1 itself is wired this is two steps away.

## Source
[classcapd_1_1dynset_1_1C1HOSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C1HOSet.html)
