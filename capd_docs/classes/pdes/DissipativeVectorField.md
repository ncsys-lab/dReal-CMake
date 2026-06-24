# DissipativeVectorField

## What it is
The **abstract interface** a PDE must implement to be integrated by `PdeSolver` — "a common
interface for a dissipative vector field" (header comment). Template
`DissipativeVectorField<SeriesT>` where `SeriesT` is a tail-bound vector type. A concrete PDE
(the only shipped one is `OneDimKSSineVectorField`) inherits and implements the pure-virtual
methods. This interface *is* the dissipative-PDE contract: it bundles ODE-coefficient
generation, the self-consistent tail bound, the tail update, and the block-norm estimates the
solver needs.

## Key API (pure-virtual unless noted; confirmed in `DissipativeVectorField.h`)
- `VectorType operator()(h, v)` and `operator()(h, v, DF&)` — evaluate the field (and its
  finite Jacobian block).
- `MatrixType derivative(h, v)` — finite square block of the field's derivative.
- `void computeODECoefficients(VectorArray& a, size_type order)` — Taylor coefficients of the
  C^0 part; overload `(const VectorArray& a, VectorArray& c, order)` for one variational column;
  a concrete `(a, J, p, numberOfColumns)` helper loops over columns.
- `void makeSelfConsistentBound(VectorArray& a)` — "refine the tail so that the vector field
  points inwards the tail" (the **isolation** property dissipativity provides); C^1 overload
  `(a, J1, J2, numberOfColumns)`.
- `size_type dimension()` — number of explicitly-tracked leading modes.
- `size_type firstDissipativeIndex()` — index above which every mode is contracting; the solver
  splits its enclosure strategy at this boundary.
- `void updateTail(VectorType& x, const VectorArray& enc, ScalarType h)` — advance the C^0 tail
  by "a linear differential inequality … under the assumption that isolation is satisfied"
  (header); C^1 overload for the variational blocks.
- `MatrixType blockNorms(const VectorType& a, size_type m)` — matrix whose diagonal is the
  logarithmic norm of each diagonal block and off-diagonals are block norms (drives the
  `matrixExp` growth bound in `encloseC1Map`).

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** No `src/` reference.

## Why it might matter
This is the seam where a new PDE enters CAPD. To expose a dissipative-PDE constraint, dReal would
implement this interface for the SMT-described system — the analogue of building a `capd::IMap`
from a vector-field string in `CapdOdeCache`, but heavier: the user must supply ODE-coefficient
recursions, an isolation/self-consistency refinement, and a tail-update inequality, not just an
expression tree. That implementation burden is the main reason only KS ships.

## Source
[classcapd_1_1pdes_1_1DissipativeVectorField.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1DissipativeVectorField.html)
(header: `capd/pdes/DissipativeVectorField.h`)
