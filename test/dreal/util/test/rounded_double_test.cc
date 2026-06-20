/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License").
*/
#include "dreal/util/rounded_interval.h"

#include <cfenv>

#include <gtest/gtest.h>

#include "ibex.h"

#include "dreal/util/rounding.h"

namespace dreal {
namespace {

// make_sound_interval just wires the typed endpoints into an ibex::Interval.
TEST(RoundedDoubleTest, MakeSoundIntervalWiresEndpoints) {
  const ibex::Interval iv{make_sound_interval(RoundedDown{1.0}, RoundedUp{2.0})};
  EXPECT_EQ(iv.lb(), 1.0);
  EXPECT_EQ(iv.ub(), 2.0);
}

// The directed-rounding helpers must match gaol's own sound enclosure, computed
// under FE_UPWARD via the negation trick (no fesetround). For inexact operands
// (0.1, 0.3 are not FP-representable), sub_down/add_up must equal the lb/ub of
// the corresponding ibex interval operation exactly.
TEST(RoundedDoubleTest, DirectedRoundingMatchesGaolEnclosure) {
  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const double a{0.3};
  const double b{0.1};

  const ibex::Interval ia{a};
  const ibex::Interval ib{b};

  EXPECT_EQ(sub_down(Exact{a}, Exact{b}, ur).value(), (ia - ib).lb());
  EXPECT_EQ(sub_up(Exact{a}, Exact{b}, ur).value(), (ia - ib).ub());
  EXPECT_EQ(add_down(Exact{a}, Exact{b}, ur).value(), (ia + ib).lb());
  EXPECT_EQ(add_up(Exact{a}, Exact{b}, ur).value(), (ia + ib).ub());
}

// The sound interval [sub_down(mid,half), add_up(mid,half)] must CONTAIN the
// true real [mid-half, mid+half]. This is the Tighten construction; the old
// hand-built Box::Interval(mid-half, mid+half) under FE_UPWARD pulled the lower
// endpoint inward (too narrow). Verify outward rounding: the sound lower bound
// is <= the nearest-rounded value and the sound upper bound is >=.
TEST(RoundedDoubleTest, TightenConstructionRoundsOutward) {
  const double mid{0.1};        // not FP-representable
  const double half{0.3 / 2.0}; // 0.15-ish, also inexact

  double nearest_lo{};
  double nearest_hi{};
  {
    const NearestRoundingScope g;
    nearest_lo = mid - half;
    nearest_hi = mid + half;
  }

  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const ibex::Interval sound{make_sound_interval(
      sub_down(Exact{mid}, Exact{half}, ur), add_up(Exact{mid}, Exact{half}, ur))};

  // Outward: lower bound rounded down (<= nearest), upper rounded up (>=).
  EXPECT_LE(sound.lb(), nearest_lo);
  EXPECT_GE(sound.ub(), nearest_hi);
  // And it is a non-degenerate, valid interval.
  EXPECT_LE(sound.lb(), sound.ub());
}

// Exact::half halves without rounding (÷2 only decrements the exponent), so it
// is mode-invariant: identical bits regardless of the FPU rounding mode.
TEST(RoundedDoubleTest, ExactHalfIsModeInvariant) {
  double up_half{};
  double near_half{};
  {
    const UpwardRoundingScope g;
    up_half = Exact{0.1}.half().value();
  }
  {
    const NearestRoundingScope g;
    near_half = Exact{0.1}.half().value();
  }
  EXPECT_EQ(up_half, near_half);
  EXPECT_EQ(Exact{1.0}.half().value(), 0.5);
}

}  // namespace
}  // namespace dreal
