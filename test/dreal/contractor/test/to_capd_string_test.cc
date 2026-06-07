// Direct unit tests for the dReal Expression → capd::IMap string converter
// (src/dreal/contractor/odes/to_capd_string.h).
//
// These tests verify the per-ExpressionKind translation rules in isolation,
// independent of any actual CAPD integration. Use string-contains rather
// than exact equality where map iteration order is not guaranteed
// (Addition's expr_to_coeff_map, Multiplication's base_to_exponent_map).

#include "dreal/contractor/odes/to_capd_string.h"

#include <cmath>
#include <string>

#include <gtest/gtest.h>

#include "dreal/symbolic/symbolic.h"

namespace dreal {
namespace {

using std::string;

// ===========================================================================
// Constants
// ===========================================================================

TEST(ToCapdStringTest, ConstantPositive) {
  // std::to_string(3.5) gives "3.500000"; no 'e', no leading '-' → as-is.
  EXPECT_EQ(to_capd_string(3.5), "3.500000");
}

TEST(ToCapdStringTest, ConstantZero) {
  EXPECT_EQ(to_capd_string(0.0), "0.000000");
}

TEST(ToCapdStringTest, ConstantNegativeWrappedInParens) {
  // Leading '-' must be wrapped so the IMap parser doesn't choke on
  // unary-minus next to a binary operator (e.g. "+(-x)" vs "+-x").
  EXPECT_EQ(to_capd_string(-3.5), "(-3.500000)");
}

TEST(ToCapdStringTest, ConstantScientificNotationRoundTrip) {
  // 1e-10 in std::to_string would be "1.000000e-10" — has 'e'; the
  // converter must round-trip through std::stod + std::fixed to drop the
  // scientific notation (CAPD's parser is conservative about exponent forms).
  const string s = to_capd_string(1e-10);
  EXPECT_EQ(s.find('e'), string::npos)
      << "scientific notation slipped through: " << s;
  EXPECT_EQ(s.find('E'), string::npos);
}

TEST(ToCapdStringTest, ConstantLargeScientificRoundTrip) {
  const string s = to_capd_string(1.5e10);
  EXPECT_EQ(s.find('e'), string::npos)
      << "large scientific notation slipped through: " << s;
}

TEST(ToCapdStringTest, ConstantNegativeScientific) {
  // After round-trip, leading '-' must still be wrapped.
  const string s = to_capd_string(-2.5e-5);
  EXPECT_EQ(s.find('e'), string::npos);
  EXPECT_EQ(s.front(), '(');
  EXPECT_EQ(s.back(), ')');
}

// ===========================================================================
// Variables
// ===========================================================================

TEST(ToCapdStringTest, VariableEmitsName) {
  const Variable x{"x_var", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(Expression{x}), "x_var");
}

// ===========================================================================
// Addition
// ===========================================================================

TEST(ToCapdStringTest, AdditionWithConstant) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  // 2 + x = Add(constant=2, x->1).
  const string s = to_capd_string(2.0 + Expression{x});
  // Form: "(2.000000+1.000000*x)" — outer parens, constant first.
  EXPECT_EQ(s.front(), '(');
  EXPECT_EQ(s.back(), ')');
  EXPECT_NE(s.find("2.000000"), string::npos);
  EXPECT_NE(s.find("*x"), string::npos);
}

TEST(ToCapdStringTest, AdditionTwoVarsCommutativeSafe) {
  // x + y can map-iterate in either order. Just check both terms appear.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(Expression{x} + Expression{y});
  EXPECT_NE(s.find("*x"), string::npos);
  EXPECT_NE(s.find("*y"), string::npos);
  // No spurious scientific notation should appear for unit coefficients.
  EXPECT_EQ(s.find('e'), string::npos);
}

TEST(ToCapdStringTest, AdditionWithNegativeCoefficient) {
  // -3 * x + 1 should produce a negative-wrapped coefficient.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(1.0 - 3.0 * Expression{x});
  // The "-3" coefficient gets wrapped: "(-3.000000)*x"
  EXPECT_NE(s.find("(-3.000000)*x"), string::npos)
      << "missing negative coefficient wrap in: " << s;
}

// ===========================================================================
// Multiplication
// ===========================================================================

TEST(ToCapdStringTest, MultiplicationConstantAndVariable) {
  // 4 * x → Mul(constant=4, x->1).
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(4.0 * Expression{x});
  // Form: "(4.000000*(x^1.000000))"
  EXPECT_EQ(s.front(), '(');
  EXPECT_EQ(s.back(), ')');
  EXPECT_NE(s.find("4.000000"), string::npos);
  EXPECT_NE(s.find("x^1.000000"), string::npos);
}

TEST(ToCapdStringTest, MultiplicationTwoVarsCommutativeSafe) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(Expression{x} * Expression{y});
  EXPECT_NE(s.find("x^1.000000"), string::npos);
  EXPECT_NE(s.find("y^1.000000"), string::npos);
}

// ===========================================================================
// Division
// ===========================================================================

TEST(ToCapdStringTest, DivisionEmitsSlashedForm) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(Expression{x} / Expression{y});
  EXPECT_EQ(s, "(x/y)");
}

// ===========================================================================
// Pow
// ===========================================================================

TEST(ToCapdStringTest, PowIntegerExponent) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  // pow(x, 2) on dReal sometimes flows into Multiplication (x*x); use
  // the explicit `pow` builder which goes through ExpressionKind::Pow.
  const string s = to_capd_string(pow(Expression{x}, 3.0));
  EXPECT_NE(s.find("x^3.000000"), string::npos)
      << "expected x^3.000000 in: " << s;
}

// ===========================================================================
// Transcendentals
// ===========================================================================

TEST(ToCapdStringTest, ExpEmitsExpFunction) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(exp(Expression{x})), "exp(x)");
}

TEST(ToCapdStringTest, SinAndCosPassThrough) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(sin(Expression{x})), "sin(x)");
  EXPECT_EQ(to_capd_string(cos(Expression{x})), "cos(x)");
}

TEST(ToCapdStringTest, TanRewrittenAsSinDivCos) {
  // CAPD's IMap doesn't accept tan directly; converter must rewrite.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(tan(Expression{x})), "(sin(x)/cos(x))");
}

TEST(ToCapdStringTest, AbsRewrittenAsSqrtSqr) {
  // CAPD's IMap doesn't accept abs directly; converter must rewrite as
  // sqrt(sqr(x)). This is mathematically identical for real x.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(abs(Expression{x})), "sqrt(sqr(x))");
}

TEST(ToCapdStringTest, SqrtEmitsSqrt) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(sqrt(Expression{x})), "sqrt(x)");
}

TEST(ToCapdStringTest, LogEmitsLog) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(log(Expression{x})), "log(x)");
}

TEST(ToCapdStringTest, AsinAcosAtanPassThrough) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(to_capd_string(asin(Expression{x})), "asin(x)");
  EXPECT_EQ(to_capd_string(acos(Expression{x})), "acos(x)");
  EXPECT_EQ(to_capd_string(atan(Expression{x})), "atan(x)");
}

TEST(ToCapdStringTest, SinhRewrittenViaExp) {
  // sinh(x) = (exp(x) - exp(-x))/2 in the converter's rewrite.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(sinh(Expression{x}));
  EXPECT_NE(s.find("exp(x)"), string::npos);
  EXPECT_NE(s.find("exp(-x)"), string::npos);
  EXPECT_NE(s.find("- "), string::npos);  // ensures '-' separator present
  EXPECT_NE(s.find("/ 2"), string::npos);
}

TEST(ToCapdStringTest, CoshRewrittenViaExp) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(cosh(Expression{x}));
  EXPECT_NE(s.find("exp(x)"), string::npos);
  EXPECT_NE(s.find("exp(-x)"), string::npos);
  EXPECT_NE(s.find("+ "), string::npos);
  EXPECT_NE(s.find("/ 2"), string::npos);
}

TEST(ToCapdStringTest, TanhRewrittenViaExp) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = to_capd_string(tanh(Expression{x}));
  EXPECT_NE(s.find("exp(x)"), string::npos);
  EXPECT_NE(s.find("exp(-x)"), string::npos);
  // tanh = (e^x - e^-x) / (e^x + e^-x): both ops present.
  EXPECT_NE(s.find("-"), string::npos);
  EXPECT_NE(s.find("+"), string::npos);
}

// ===========================================================================
// Unsupported expression kinds throw
// ===========================================================================

TEST(ToCapdStringTest, IfThenElseThrows) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  // build an ITE expression: if x>0 then x else y
  const Expression ite_expr = if_then_else(
      Expression{x} > 0.0, Expression{x}, Expression{y});
  EXPECT_THROW({ (void)to_capd_string(ite_expr); }, std::runtime_error);
}

// ===========================================================================
// Composition smoke test (ODE-RHS-shaped expression).
// ===========================================================================

TEST(ToCapdStringTest, CompositeMockProstateRhs) {
  // dx/dt for the mock-prostate fixture: -x * (z/(z+2)).
  // This is the kind of expression that builds a multi-level Mul tree —
  // verify the converter doesn't drop terms.
  const Variable xv{"x", Variable::Type::CONTINUOUS};
  const Variable zv{"z", Variable::Type::CONTINUOUS};
  const Expression rhs =
      -Expression{xv} * (Expression{zv} / (Expression{zv} + 2.0));
  const string s = to_capd_string(rhs);
  // All three variables/constants must appear somewhere.
  EXPECT_NE(s.find("x^1"), string::npos);
  // z appears twice in the RHS; at least one occurrence must survive.
  EXPECT_NE(s.find("z"), string::npos);
  EXPECT_NE(s.find("2.000000"), string::npos);
  // No 'e' in scientific notation.
  EXPECT_EQ(s.find('e'), s.find("exp"))  // 'e' may legally appear inside "exp"
      << "unexpected scientific notation in: " << s;
}

}  // namespace
}  // namespace dreal
