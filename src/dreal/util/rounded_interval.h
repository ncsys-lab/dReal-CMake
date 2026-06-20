/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#include <cfenv>
#include <utility>

#include "ibex.h"

#include "dreal/util/assert.h"
#include "dreal/util/rounding.h"

namespace dreal {

// Interval-regime (FE_UPWARD) token consumers: the gaol/ibex operations sound
// only under FE_UPWARD, each gated by the UpwardRounding capability token (see
// rounding.h). This header is the upward-regime mirror of rounded_format.h /
// json_guarded.h (the nearest-regime consumers).
//
// Typed directed-rounding doubles — the scalar analog of the UpwardRounding
// capability token (see rounding.h).
//
// The hazard these prevent: hand-written scalar `double` arithmetic that feeds
// an interval endpoint. A sound interval [lo, hi] needs lo rounded toward -inf
// and hi rounded toward +inf — opposite directions, impossible under one
// ambient mode. Writing `ibex::Interval(mid - half, mid + half)` is therefore
// mis-rounded under *every* single rounding mode (the lower endpoint is pulled
// inward), silently producing a too-narrow interval. Encoding the rounding
// direction in the type makes the compiler check it: make_sound_interval()
// only accepts a RoundedDown lower bound and a RoundedUp upper bound.
//
// All directed arithmetic runs under FE_UPWARD (gaol's regime) and computes the
// round-down direction via the negation identity roundDown(x) = -roundUp(-x),
// so there is no per-op fesetround. The UpwardRounding token parameter is the
// compile-time proof that FE_UPWARD is established.

/// A double whose value does not depend on the FPU rounding mode (a literal, an
/// exact power-of-two scaling, an integer-valued result). Usable as either
/// rounding direction.
class Exact {
 public:
  explicit constexpr Exact(double v) : v_{v} {}
  constexpr double value() const { return v_; }

  /// delta/2 is exact in IEEE-754 (it only decrements the exponent), so halving
  /// an Exact stays Exact regardless of rounding mode.
  constexpr Exact half() const { return Exact{v_ * 0.5}; }

 private:
  double v_;
};

/// A double that is <= the exact real value it approximates (rounded -inf).
class RoundedDown {
 public:
  explicit constexpr RoundedDown(double v) : v_{v} {}
  constexpr double value() const { return v_; }

 private:
  double v_;
};

/// A double that is >= the exact real value it approximates (rounded +inf).
class RoundedUp {
 public:
  explicit constexpr RoundedUp(double v) : v_{v} {}
  constexpr double value() const { return v_; }

 private:
  double v_;
};

/// Round (a - b) toward +inf. Under FE_UPWARD, scalar subtraction already
/// rounds toward +inf.
inline RoundedUp sub_up(Exact a, Exact b, const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return RoundedUp{a.value() - b.value()};
}

/// Round (a - b) toward -inf, via roundDown(a-b) = -roundUp(b-a). Computed
/// under FE_UPWARD with no mode switch.
inline RoundedDown sub_down(Exact a, Exact b, const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return RoundedDown{-(b.value() - a.value())};
}

/// Round (a + b) toward +inf. Under FE_UPWARD, scalar addition rounds +inf.
inline RoundedUp add_up(Exact a, Exact b, const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return RoundedUp{a.value() + b.value()};
}

/// Round (a + b) toward -inf, via roundDown(a+b) = -roundUp(-a-b).
inline RoundedDown add_down(Exact a, Exact b, const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return RoundedDown{-((-a.value()) - b.value())};
}

/// Build a sound interval from an outward-rounded endpoint pair. The types make
/// it impossible to pass a mis-rounded endpoint.
inline ibex::Interval make_sound_interval(RoundedDown lo, RoundedUp hi) {
  DREAL_ASSERT(lo.value() <= hi.value());
  return ibex::Interval{lo.value(), hi.value()};
}

/// FE_UPWARD-guarded accessors. ibex::Interval::mid()/diam() are gaol
/// directed-rounding computations (NOT pure stored-value reads like lb()/ub())
/// and are sound only under FE_UPWARD. The UpwardRounding token is the
/// compile-time proof of the mode (uniform with sub_*/add_*/ibex_hc4_backward);
/// the assert is the Debug drift backstop. The rounding lint forbids raw .mid()/
/// .diam() so every call is routed here.
inline double safe_mid(const ibex::Interval& iv, const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return iv.mid();
}

inline double safe_diam(const ibex::Interval& iv, const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return iv.diam();
}

/// IBEX HC4 backward contraction (the forward-backward workhorse). @p cb is the
/// per-variable narrowing callback. Returns true if the box was already inner.
///
/// gaol is sound only under FE_UPWARD; the UpwardRounding token is the
/// compile-time proof. Routing every raw ibex arithmetic call through a wrapper
/// that *requires* the token lets the rounding lint forbid any direct
/// ibex::Function::backward call (such a call could not prove its rounding
/// mode). This closes the gap the per-Prune token alone leaves: the token gates
/// Prune entry; this wrapper gates the arithmetic itself.
template <typename Callback>
bool ibex_hc4_backward(const ibex::Function& f, const ibex::Domain& rhs,
                       ibex::IntervalVector& box, Callback&& cb,
                       const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return f.backward(rhs, box, std::forward<Callback>(cb));
}

}  // namespace dreal
