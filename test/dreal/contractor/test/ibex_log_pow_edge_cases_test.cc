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

// dreal/dreal4#321, the ibex HC4 backward chain (FIXED by the gaol-fork
// underflow_saturate patch). For `x - pow(0.5_const, 1075) = 0` with x in
// [DBL_TRUE_MIN, +inf], forward pow gives [0, DBL_TRUE_MIN] (sound), sub-backward
// pins the pow output to the singleton {DBL_TRUE_MIN}, and pre-fix bwd_pow then
// inverted it via the *tight* gaol root (~0.50034), excluding the base 0.5 and
// emptying the box -> false unsat. underflow_saturate widens the subnormal-band
// target to include 0 before the root, restoring forward/backward consistency.
// Variant A (sub-only) was always sound; B (pow 1075) is the regression; C (pow
// 1074, exactly DBL_TRUE_MIN) is the just-above-underflow sanity. Note: ibex
// patch #11 made Function::backward return-status (no EmptyBoxException), so we
// assert the bool result rather than catch a throw.
TEST(IbexLogPowEdgeCases, PowSubnormalUnderflowEndToEnd) {
  std::fesetround(FE_UPWARD);  // match gaol's runtime mode
  const double kDenormMin = std::numeric_limits<double>::denorm_min();

  // Each variant: x - <opexpr> = 0 with x >= DBL_TRUE_MIN must leave x non-empty
  // (the forward relaxation deems DBL_TRUE_MIN feasible, so backward must not
  // prune it). The soundness property is the *queried variable box* staying
  // non-empty; that is what dReal's solver checks (Box::is_empty), per ibex
  // patch #11. NB: Function::backward's bool return-status is an internal
  // "a domain (possibly a degenerate constant node) was contradicted" signal
  // and is independently false here for all three variants -- including the
  // pow-free variant A -- so it is NOT the emptiness signal and we don't assert
  // on it.
  auto check = [&](const char* tag, const ibex::ExprNode& opexpr) {
    ibex::Variable x_("x");
    ibex::Function f(x_, x_ - opexpr);
    ibex::IntervalVector box(1);
    box[0] = ibex::Interval(kDenormMin, kInf);
    f.backward(ibex::Interval::zero(), box);
    EXPECT_FALSE(box[0].is_empty())
        << tag << " left x empty: [" << box[0].lb() << ", " << box[0].ub()
        << "].";
  };

  check("[A sub-only]",
        ibex::ExprConstant::new_scalar(ibex::Interval(0.0, kDenormMin)));
  check("[B pow(0.5,1075) #321]",
        ibex::pow(ibex::ExprConstant::new_scalar(0.5), 1075));
  check("[C pow(0.5,1074) sanity]",
        ibex::pow(ibex::ExprConstant::new_scalar(0.5), 1074));
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

// ===========================================================================
// dreal/dreal4#321 backward-consistency net (FIXED by underflow_saturate).
// For each HC4 backward op, force the op-output to its forward-image extreme (a
// subnormal) so sub-backward pins it to that singleton, then check the operand
// domain is NOT wrongly emptied by a too-tight inverse. The forward relaxation
// deems the extreme feasible, so a sound backward must leave the operand
// non-empty; pre-fix the five tight-inverting ops (pow/exp/sqr/mul/div) emptied
// it -> false unsat. sqrt/log invert via a loose forward op and were always
// sound (controls). EXPECT_FALSE(empty) is the required soundness property.
// ===========================================================================
TEST(IbexLogPowEdgeCases, SubnormalBackwardStaysConsistent) {
  std::fesetround(FE_UPWARD);
  const double kDenormMin = std::numeric_limits<double>::denorm_min();

  auto probe = [](const char* tag, const ibex::ExprNode& opnode,
                  const ibex::Interval& fwd_img, bool force_upper) {
    ibex::Variable x_("x");
    ibex::Function f(x_, x_ - opnode);
    ibex::IntervalVector box(1);
    // Force the op-output to the forward-image extreme (the subnormal ceiling
    // for positive images; the floor for negative ones) so sub-backward pins it
    // to a singleton and the operand's tight inverse is exercised.
    box[0] = force_upper ? ibex::Interval(fwd_img.ub(), kInf)
                         : ibex::Interval(-kInf, fwd_img.lb());
    f.backward(ibex::Interval::zero(), box);
    EXPECT_FALSE(box[0].is_empty())
        << "[" << tag << "] EMPTIED: fwd_img=[" << fwd_img.lb() << ", "
        << fwd_img.ub() << "] -> box now empty -- backward too tight (bug)";
  };

  // exp(c) for c<-744 underflows to a subnormal; bwd_exp inverts via tight log.
  probe("exp(-745)", ibex::exp(ibex::ExprConstant::new_scalar(-745.0)),
        ibex::exp(ibex::Interval(-745.0)), true);
  // sqr(1e-160) = 1e-320 underflows to a subnormal; bwd_sqr inverts via sqrt_rel.
  probe("sqr(1e-160)", ibex::sqr(ibex::ExprConstant::new_scalar(1e-160)),
        ibex::sqr(ibex::Interval(1e-160)), true);
  // pow(0.5,1075) -- the known #321 base case, re-probed via this harness.
  probe("pow(0.5,1075)", ibex::pow(ibex::ExprConstant::new_scalar(0.5), 1075),
        ibex::pow(ibex::Interval(0.5), 1075), true);
  // sqrt control: sqrt of a positive c never produces a subnormal (expect sound).
  probe("sqrt(denorm)", ibex::sqrt(ibex::ExprConstant::new_scalar(kDenormMin)),
        ibex::sqrt(ibex::Interval(kDenormMin)), true);
  // log control: log of a tiny c is a large-negative point; bwd_log inverts via
  // exp (which underflows). Force the floor (most-negative) side.
  probe("log(denorm)", ibex::log(ibex::ExprConstant::new_scalar(kDenormMin)),
        ibex::log(ibex::Interval(kDenormMin)), false);
  // mul: 1e-160 * 1e-162 = 1e-322 lands in the subnormal range; bwd_mul inverts
  // each factor via gaol's tight div_rel.
  probe("mul(1e-160,1e-162)",
        ibex::ExprConstant::new_scalar(1e-160) *
            ibex::ExprConstant::new_scalar(1e-162),
        ibex::Interval(1e-160) * ibex::Interval(1e-162), true);
  // div: 1e-320 / 1e3 = 1e-323 is subnormal; bwd_div inverts via mul + div_rel.
  probe("div(1e-320,1e3)",
        ibex::ExprConstant::new_scalar(1e-320) /
            ibex::ExprConstant::new_scalar(1e3),
        ibex::Interval(1e-320) / ibex::Interval(1e3), true);
}

}  // namespace
}  // namespace dreal
