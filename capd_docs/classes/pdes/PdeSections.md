# PDE Poincaré Sections: PdeAbstractSection · PdeAffineSection · PdeCoordinateSection

## What they are
**Poincaré sections** for the PDE phase space — the infinite-dim analogues of CAPD's ODE
sections ([poincare-maps](../../concepts/poincare-maps.md)). A section is a hypersurface
`{ s(x) = 0 }` in the (leading-modes + tail) state space; combined with a `PdeSolver` it defines a
section-crossing (Poincaré) map. Templated on `<VectorT, MatrixT>` where `VectorT` is a
tail-bound series type.

- **`PdeAbstractSection<VectorT,MatrixT>`** — the abstract base: pure-virtual `operator()(v)`
  (section value), `gradient(u)`, `gradientByVector(x,u)`, `getProjection(size_type)`. Provides
  `isSpecialSection()` (default `false`), `evalAt(set)`, and a concrete `computeDP(...)` that
  computes the derivative of the Poincaré map and the return-time gradient from a finite Jacobian
  block plus tail-block data `Dy`/`Dyx` (confirmed `PdeAbstractSection.h`).
- **`PdeAffineSection`** — an **affine** section `⟨n, x − x0⟩ = 0` given by a point and a normal
  vector; `gradient` returns the (constant) normal lifted to a series; built on a finite-dim
  affine section over the explicit coefficients (confirmed `PdeAffineSection.h`).
- **`PdeCoordinateSection`** — the special section `{ x_i = c }` (one coordinate equals a
  constant); `isSpecialSection()` returns `true` (so the Poincaré map can delegate the value
  computation to the set's own representation), gradient is the i-th unit direction (confirmed
  `PdeCoordinateSection.h`).

Supporting structs (namespace page): `PdeSectionDerivativesEnclosure<VectorT,MatrixT>` and
`ComputeOneStepSectionEnclosure<isC1>` — the one-step section-crossing enclosure machinery.

## Key API
See per-class above; common surface is `operator()(v)` (evaluate section), `gradient(u)`,
`gradientByVector(x,u)`, `getProjection(...)`, `isSpecialSection()`, and the base's
`computeDP(...)` / `evalAt(set)`. (All confirmed in the four headers.)

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** No `src/` reference.

## Why it might matter
The ODE audit notes that a *pinned terminal time* is a degenerate time-section, so CAPD's
section/Poincaré machinery *might* fit dReal's terminal-gating more naturally than tube-slicing
([dreal-capd-usage.md](../../dreal-capd-usage.md) "PoincareMap / sections"). These are the PDE
versions of that lever: `PdeCoordinateSection` (`x_i = c`) is the obvious encoding of an
event/threshold condition on a PDE mode, and `computeDP` already supplies the section-crossing
derivative needed for interval-Newton sharpening. As with all PDE classes here, this is a
hypothetical integration point, not current usage.

## Source
[classcapd_1_1pdes_1_1PdeAbstractSection.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1PdeAbstractSection.html) ·
[classcapd_1_1pdes_1_1PdeAffineSection.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1PdeAffineSection.html) ·
[classcapd_1_1pdes_1_1PdeCoordinateSection.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1PdeCoordinateSection.html)
(headers: `capd/pdes/PdeAbstractSection.h`, `PdeAffineSection.h`, `PdeCoordinateSection.h`)
