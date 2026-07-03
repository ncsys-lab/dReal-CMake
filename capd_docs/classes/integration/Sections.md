# Poincaré section family (`capd::poincare::*Section`)

## What it is

The section types a `PoincareMap` crosses. Every section is a **scalar function of state**,
α: ℝⁿ → ℝ, with the section S = α⁻¹(0); none take time as an argument. (The classes share a
copy-pasted Doxygen docstring "TimeMap class provides class that serves as Poincare section…" —
that text is a comment-paste error; the `operator()(const VectorType&)` signatures are
authoritative and show state-only evaluation.)

## The family (signatures from class pages + headers)

```cpp
// abstract base — α(x), gradient, evalAt(set)
virtual ScalarType AbstractSection::operator()(const VectorType& v) const = 0;
virtual VectorType AbstractSection::gradient(const VectorType& u) const = 0;
virtual ScalarType AbstractSection::evalAt(const Set& set) const = 0;

CoordinateSection(size_type D, size_type i, ScalarType c = 0);   // section x_i = c
AffineSection(...);                 // affine hyperplane a·x = c (operator()(const VectorType&))
NonlinearSection(const std::string& s);   // arbitrary α parsed from an expression string
```

`CoordinateSection` (axis-aligned) and `AffineSection` (hyperplane) admit
optimized derivative recomputation; `NonlinearSection` parses a general expression (and
allocates its own ODE solver for the section's own dynamics). `isSpecialSection()` flags the
optimized cases. (Each constructor/operator confirmed in the respective header/class page.)

## dReal status

**Unused** (entire family). `dreal-capd-usage.md`.

## Why it might matter

Sections are the lever that would be needed for any state-event gating. The audit's specific
question — a *time* section for pinned-terminal-time gating — is **not supported**: there is no
section type with a time coordinate. Encoding "t = t*" as a section requires augmenting the
vector field with a clock variable τ (τ̇=1) and using `CoordinateSection(τ = t*)` — possible but
strictly more dimensions and a Newton solve versus the existing `TimeMap(t*, x)`, with no
documented tightness benefit. `NonlinearSection`'s string-parsed α is the only one flexible
enough for nontrivial events, at the cost of a second parsed expression per constraint.

## Source

[`../../../../CAPD/docs/html/classcapd_1_1poincare_1_1AbstractSection.html`](../../../../CAPD/docs/html/classcapd_1_1poincare_1_1AbstractSection.html)
