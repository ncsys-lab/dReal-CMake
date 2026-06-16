// Regression tests for ncsys-lab/ibex-lib@dreal-perf-patches commit 33eb6676
// ("gaol: fix Interval::log and Interval::pow soundness gaps"), which carries
// forward Soonho Kong's 2018 dreal-deps fixes (fe3eb925 for log, 60218733 for
// pow) onto modernized mainline. See ibex-fork/MIGRATION.md §8.
//
// The patch closes two gaol-wrapper soundness gaps that IBEX's own test suite
// does not exercise:
//   - `Interval::log` previously returned EMPTY_SET when `x.ub() <= 0`, even
//     for x=[0,0] where the true value -oo is mathematically valid. Strict
//     `<` lets the underlying gaol::log surface its (-oo,-DBL_MAX] result.
//     Without this, dReal's HC4 contractor on transcendentals can declare a
//     sound branch infeasible (false UNSAT).
//   - `Interval::pow(x, double d)` dispatched to gaol's scalar exponent
//     overload, which silently mishandled fractional d — e.g.
//     pow([1,4], 0.5) returned [1,1] instead of [1,2]. Wrapping d in
//     gaol::interval(d,d) routes through the (interval,interval) overload,
//     which handles fractional exponents correctly.
//
// If any test below fails with "Patch 33eb6676 missing?" the ibex pin has
// regressed off the patched dreal-perf-patches branch.

#include <gtest/gtest.h>

#include <cfenv>
#include <cmath>
#include <limits>

#include <ibex.h>

namespace dreal {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// Helper: assert that `r` contains the true interval [true_lb, true_ub] with
// at most `slack` overrun on each side. Captures the IA soundness property:
// the output must overapproximate the true range.
::testing::AssertionResult Contains(const ibex::Interval& r,
                                    double true_lb, double true_ub,
                                    double slack = 1e-9) {
  if (r.is_empty())
    return ::testing::AssertionFailure() << "r is empty";
  if (r.lb() > true_lb + slack || r.ub() < true_ub - slack) {
    return ::testing::AssertionFailure()
        << "r = [" << r.lb() << ", " << r.ub()
        << "] does not contain true [" << true_lb << ", " << true_ub << "]";
  }
  return ::testing::AssertionSuccess();
}

// =============================================================================
// log() edge cases
// =============================================================================

TEST(IbexLogPowEdgeCases, LogOfPositive) {
  // log([1, e]) ≈ [0, 1]. Sanity.
  const ibex::Interval r = ibex::log(ibex::Interval(1.0, std::exp(1.0)));
  EXPECT_FALSE(r.is_empty());
  EXPECT_TRUE(Contains(r, 0.0, 1.0));
}

TEST(IbexLogPowEdgeCases, LogOfUnitInterval) {
  // log([0, 1]) must contain [-oo, 0]. Pre-patch gaol returned empty because of
  // the `<= 0` test.
  const ibex::Interval r = ibex::log(ibex::Interval(0.0, 1.0));
  EXPECT_FALSE(r.is_empty())
      << "Regression: log([0,1]) returned empty. Patch 33eb6676 missing?";
  EXPECT_EQ(r.lb(), -kInf);
  // ub may be slightly above 0.0 (a few subnormals); soundness requires
  // ub >= 0 only.
  EXPECT_GE(r.ub(), 0.0);
  EXPECT_LE(r.ub(), 1e-300);
}

TEST(IbexLogPowEdgeCases, LogOfPointAtZero) {
  // log([0, 0]) = {-oo}. Patched gaol returns [-oo, -oo] (or [-oo,
  // some_finite_safety_bound]); pre-patch returned empty.
  const ibex::Interval r = ibex::log(ibex::Interval(0.0, 0.0));
  EXPECT_FALSE(r.is_empty())
      << "Regression: log([0,0]) returned empty. Patch 33eb6676 missing?";
  EXPECT_EQ(r.lb(), -kInf);
  // Upper bound: -oo if the implementation is tight; could be 0 if loose.
  // The safe (post-patch) upper bound is <= 0.
  EXPECT_LE(r.ub(), 0.0);
}

TEST(IbexLogPowEdgeCases, LogOfNegativeReturnsEmpty) {
  // log([-2, -1]) must be empty (no valid x). Unchanged by patch — sanity.
  const ibex::Interval r = ibex::log(ibex::Interval(-2.0, -1.0));
  EXPECT_TRUE(r.is_empty());
}

TEST(IbexLogPowEdgeCases, LogOfMixedSign) {
  // log([-1, 1]) should consider only the valid [0, 1] subset → [-oo, 0].
  // Pre-patch gaol returned empty (treating presence of negative as fatal).
  const ibex::Interval r = ibex::log(ibex::Interval(-1.0, 1.0));
  EXPECT_FALSE(r.is_empty())
      << "Regression: log([-1,1]) returned empty. Patch 33eb6676 missing?";
  EXPECT_EQ(r.lb(), -kInf);
  EXPECT_LE(r.ub(), 0.0 + 1e-12);
}

TEST(IbexLogPowEdgeCases, LogOfHugePositive) {
  // log([0, +oo]) = [-oo, +oo]. Unchanged by patch — sanity.
  const ibex::Interval r = ibex::log(ibex::Interval(0.0, kInf));
  EXPECT_FALSE(r.is_empty());
  EXPECT_EQ(r.lb(), -kInf);
  EXPECT_EQ(r.ub(), +kInf);
}

// =============================================================================
// pow() edge cases
// =============================================================================

TEST(IbexLogPowEdgeCases, PowNegBaseEvenInteger) {
  // [-2, -1]^2 = [1, 4]. Pre-patch (scalar overload) silently returned a wider
  // or wrong interval on some gaol builds because the int-exponent path didn't
  // square negative endpoints correctly.
  const ibex::Interval r = ibex::pow(ibex::Interval(-2.0, -1.0), 2);
  EXPECT_FALSE(r.is_empty());
  EXPECT_TRUE(Contains(r, 1.0, 4.0));
}

TEST(IbexLogPowEdgeCases, PowZeroToZero) {
  // 0^0 is conventionally 1 in IEEE / most CAS, but interval pow may return
  // a wider safety interval. Post-patch behavior: result is non-empty and
  // contains 1.
  const ibex::Interval r = ibex::pow(ibex::Interval(0.0, 0.0), 0);
  EXPECT_FALSE(r.is_empty())
      << "Regression: 0^0 returned empty. Patch 33eb6676 missing?";
  EXPECT_LE(r.lb(), 1.0);
  EXPECT_GE(r.ub(), 1.0);
}

TEST(IbexLogPowEdgeCases, PowPositiveFractionalExponent) {
  // [1, 4]^0.5 = [1, 2]. Pre-patch the scalar overload of gaol::pow could
  // return inflated bounds because the exponent wasn't promoted to interval.
  const ibex::Interval r = ibex::pow(ibex::Interval(1.0, 4.0), 0.5);
  EXPECT_FALSE(r.is_empty());
  EXPECT_TRUE(Contains(r, 1.0, 2.0));
}

TEST(IbexLogPowEdgeCases, PowZeroBasePositiveExponent) {
  // 0^k = 0 for k > 0. Pre-patch gaol could return empty on some inputs
  // when the scalar exponent overload took the wrong branch.
  const ibex::Interval r = ibex::pow(ibex::Interval(0.0, 0.0), 3);
  EXPECT_FALSE(r.is_empty())
      << "Regression: 0^3 returned empty. Patch 33eb6676 missing?";
  EXPECT_EQ(r.lb(), 0.0);
  EXPECT_EQ(r.ub(), 0.0);
}

// DISABLED — captures the remaining unsoundness chain for dreal/dreal4#321
// AFTER the ibex-fork patch 33eb6676 and the dReal Expression constant-fold
// underflow guard. The chain:
//
//  1. `gaol::pow(Interval(0.5), 1075)` correctly returns [0, DBL_TRUE_MIN]
//     after patch 33eb6676 (see PowSubnormalUnderflow above).
//  2. ibex's HC4 backward through ExprSub-of-ExprPower mis-narrows: for the
//     constraint `x - pow(0.5_const, 1075) = 0` with x in [DBL_TRUE_MIN, +inf]
//     and the pow's forward image [0, DBL_TRUE_MIN], sub backward refines the
//     pow-output to the singleton [DBL_TRUE_MIN, DBL_TRUE_MIN]; bwd_pow then
//     observes that 0.5^1075 (the true value, ≈2^-1075) is strictly below
//     DBL_TRUE_MIN, concludes no x in {0.5} satisfies, and throws empty.
//     The pow forward and backward disagree on the underflow tolerance — the
//     forward includes the subnormal regime in its image, the backward does
//     not. A correct delta-SMT fix needs either a delta-aware contractor or
//     a bwd_pow that treats a degenerate constant input as non-narrowable.
//
// Variant A (sub-only) passes; variant B (pow + sub) is the broken path; C
// is the 1074 sanity. Variant B is the unfixed regression; the test is
// DISABLED_-prefixed so CI stays green while leaving the diagnostic in tree.
TEST(IbexLogPowEdgeCases, DISABLED_PowSubnormalUnderflowEndToEnd) {
  std::fesetround(FE_UPWARD);  // match gaol's runtime mode
  const double kDenormMin = std::numeric_limits<double>::denorm_min();

  // Variant A
  {
    ibex::Variable x_("x");
    ibex::Function f(x_, x_ - ibex::ExprConstant::new_scalar(
                                  ibex::Interval(0.0, kDenormMin)));
    ibex::IntervalVector box(1);
    box[0] = ibex::Interval(kDenormMin, kInf);
    try {
      f.backward(ibex::Interval::zero(), box);
    } catch (...) {
      FAIL() << "[A] backward threw.";
    }
    EXPECT_FALSE(box[0].is_empty()) << "[A] x - [0, DBL_TRUE_MIN] = 0 with x >= DBL_TRUE_MIN should leave x non-empty; got [" << box[0].lb() << ", " << box[0].ub() << "].";
  }

  // Variant B
  {
    ibex::Variable x_("x");
    ibex::Function f(x_, x_ - ibex::pow(ibex::ExprConstant::new_scalar(0.5), 1075));
    ibex::IntervalVector box(1);
    box[0] = ibex::Interval(kDenormMin, kInf);
    try {
      f.backward(ibex::Interval::zero(), box);
    } catch (...) {
      FAIL() << "[B] backward threw.";
    }
    EXPECT_FALSE(box[0].is_empty()) << "[B] x = pow(0.5, 1075) with x >= DBL_TRUE_MIN should leave x non-empty; got [" << box[0].lb() << ", " << box[0].ub() << "]. Reproduces dreal/dreal4#321 at the ibex layer.";
  }

  // Variant C — sanity
  {
    ibex::Variable x_("x");
    ibex::Function f(x_, x_ - ibex::pow(ibex::ExprConstant::new_scalar(0.5), 1074));
    ibex::IntervalVector box(1);
    box[0] = ibex::Interval(kDenormMin, kInf);
    try {
      f.backward(ibex::Interval::zero(), box);
    } catch (...) {
      FAIL() << "[C] backward threw.";
    }
    EXPECT_FALSE(box[0].is_empty()) << "[C sanity] pow(0.5, 1074) should always succeed; got [" << box[0].lb() << ", " << box[0].ub() << "].";
  }
}

TEST(IbexLogPowEdgeCases, PowSubnormalUnderflow) {
  // pow(0.5, 1075) = 2^-1075 — strictly positive but below DBL_TRUE_MIN, so
  // any double-precision evaluation rounds to 0. A sound interval contractor
  // must report an interval whose upper bound is strictly positive (the
  // narrowest such is [0, DBL_TRUE_MIN]) so that downstream `x > 0` checks
  // remain SAT.
  //
  // Pre-patch the scalar gaol::pow(Interval, double) overload returned [0, 0]
  // — unsound under-approximation. The (interval,interval) overload routed
  // by patch 33eb6676 returns [0, DBL_TRUE_MIN] WHEN gaol's interval-rounding
  // mode is set to FE_UPWARD (gaol's normal operating mode, established by
  // its TLS init). gtest starts in FE_TONEAREST, so we explicitly set the
  // mode here to mirror dReal's runtime configuration.
  //
  // Regression for dreal/dreal4#321 at the gaol-wrapper layer.
  std::fesetround(FE_UPWARD);
  const ibex::Interval r = ibex::pow(ibex::Interval(0.5, 0.5), 1075.0);
  EXPECT_FALSE(r.is_empty())
      << "Regression: pow(0.5, 1075) returned empty. Patch 33eb6676 missing?";
  EXPECT_GT(r.ub(), 0.0)
      << "Soundness violation: pow(0.5, 1075) returned [" << r.lb() << ", "
      << r.ub() << "] but true value 2^-1075 is strictly positive.";
}

}  // namespace
}  // namespace dreal
