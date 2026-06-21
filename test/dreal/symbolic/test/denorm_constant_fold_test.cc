// Layer-2 (Drake symbolic AST) coverage for the constant-underflow facet of
// dreal/dreal4#321. A constant fold of pow/mul/div whose result *underflowed* a
// nonzero true value to +/-0 (or overflowed a finite one to +/-inf) must not be
// the lying scalar literal -- e.g. pow(0.5,1075) = 2^-1075 rounds to 0.0, so
// pow(0.5,1075) > 0 would be a false unsat. `sound_constant_fold`
// (symbolic_expression.cc) instead folds to a sound RealConstant *interval*
// bracketing the true value (sign from std::signbit), which stays a constant
// (Drake forbids a symbolic Pow/Mul with all-constant operands) yet is sound.
//
// NB on parser kinds: the SMT2 parser builds an exactly-representable literal
// (0.5, integers) as a Constant (double) -> these folds fire and are made sound
// here; an inexact decimal (0.1) is built as a RealConstant (interval) -> never
// folds in the first place. Both are exercised below.

#include "dreal/symbolic/symbolic.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace dreal {
namespace {

constexpr double kDenormMin = std::numeric_limits<double>::denorm_min();

// --- Constant-kind (Expression{double}) folds: now sound intervals ----------

TEST(DenormConstantFold, PowUnderflowFoldsToSoundInterval) {
  const Expression e{pow(Expression{0.5}, Expression{1075.0})};
  EXPECT_FALSE(is_constant(e, 0.0)) << "must not fold to the lying scalar 0.0";
  ASSERT_TRUE(is_real_constant(e)) << "underflow must fold to a RealConstant.";
  EXPECT_GE(get_lb_of_real_constant(e), 0.0);
  EXPECT_GT(get_ub_of_real_constant(e), 0.0)
      << "the interval upper bound must be strictly positive (true value > 0).";
  EXPECT_LE(get_ub_of_real_constant(e), kDenormMin);
}

TEST(DenormConstantFold, NegativePowUnderflowKeepsSign) {
  // (-0.5)^1075 = -2^-1075 underflows to -0.0; the sound interval must be <= 0
  // (negative side), not the positive bracket.
  const Expression e{pow(Expression{-0.5}, Expression{1075.0})};
  ASSERT_TRUE(is_real_constant(e));
  EXPECT_LT(get_lb_of_real_constant(e), 0.0);
  EXPECT_LE(get_ub_of_real_constant(e), 0.0);
}

TEST(DenormConstantFold, DivUnderflowFoldsToSoundInterval) {
  const Expression e{Expression{1e-320} / Expression{1e10}};  // 1e-330 -> 0
  ASSERT_TRUE(is_real_constant(e));
  EXPECT_GT(get_ub_of_real_constant(e), 0.0);
}

TEST(DenormConstantFold, MulUnderflowFoldsToSoundInterval) {
  const Expression e{Expression{1e-200} * Expression{1e-150}};  // 1e-350 -> 0
  ASSERT_TRUE(is_real_constant(e));
  EXPECT_GT(get_ub_of_real_constant(e), 0.0);
}

TEST(DenormConstantFold, OverflowFoldsToSoundInterval) {
  const Expression e{pow(Expression{10.0}, Expression{400.0})};  // 1e400 -> inf
  ASSERT_TRUE(is_real_constant(e));
  EXPECT_GE(get_lb_of_real_constant(e), std::numeric_limits<double>::max());
}

// --- Controls: faithful folds and the RealConstant (inexact-literal) path ----

TEST(DenormConstantFold, FaithfulPowStillFoldsToScalar) {
  EXPECT_TRUE(is_constant(pow(Expression{2.0}, Expression{3.0}), 8.0))
      << "a faithful fold must still produce the scalar constant 8.";
}

TEST(DenormConstantFold, GenuineZeroPowStillFolds) {
  EXPECT_TRUE(is_constant(pow(Expression{0.0}, Expression{3.0}), 0.0))
      << "0^3 = 0 is a faithful zero and must fold to the scalar 0.";
}

TEST(DenormConstantFold, RealConstantPowNeverFolds) {
  // The inexact-literal path. A RealConstant is a *non-degenerate one-ULP*
  // bracket (the parser builds it only for i.diam()>0); is_constant is false for
  // it, so the fold never fires and pow stays symbolic.
  const Expression base{real_constant(0.1, std::nextafter(0.1, 1.0), true)};
  const Expression e{pow(base, Expression{1075.0})};
  EXPECT_TRUE(is_pow(e)) << "RealConstant pow stays symbolic (never folds).";
}

}  // namespace
}  // namespace dreal
