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
#include "dreal/util/rounding.h"

namespace dreal {
namespace {

using std::string;

// to_capd_string renders doubles to decimal and (since the precision fix) takes
// a NearestRounding token: a binary->decimal conversion is correctly rounded
// only in round-to-nearest, and in production it is always reached under
// make_capd_ode_cache's NearestRoundingScope, whose token it threads down. These
// tests call it directly, so they must establish nearest themselves — otherwise
// a preceding test that leaves the FPU in FE_UPWARD makes the 17-significant-
// digit formatting mis-round the last digit (so the round-trip check fails) and,
// in a Debug build, trips the DREAL_ASSERT_ROUNDING in to_capd_string. The
// fixture's scope member pins the regime and mints the token for every case via
// the capd_str helper, so the bodies stay focused on the conversion rules.
class ToCapdStringTest : public ::testing::Test {
 protected:
  const NearestRoundingScope nearest_{};
  template <class T>
  string capd_str(const T& x) const {
    return to_capd_string(x, nearest_.token());
  }
};

// ===========================================================================
// Constants
// ===========================================================================

TEST_F(ToCapdStringTest, ConstantPositive) {
  // Round-trippable default-format render (max_digits10 sig figs); an exactly
  // representable value like 3.5 emits its short form, no 'e', no leading '-'.
  EXPECT_EQ(capd_str(3.5), "3.5");
}

TEST_F(ToCapdStringTest, ConstantZero) {
  EXPECT_EQ(capd_str(0.0), "0");
}

TEST_F(ToCapdStringTest, ConstantNegativeWrappedInParens) {
  // Leading '-' must be wrapped so the IMap parser doesn't choke on
  // unary-minus next to a binary operator (e.g. "+(-x)" vs "+-x").
  EXPECT_EQ(capd_str(-3.5), "(-3.5)");
}

TEST_F(ToCapdStringTest, ConstantScientificNotationRoundTrip) {
  // 1e-10 in std::to_string would be "1.000000e-10" — has 'e'; the
  // converter must round-trip through std::stod + std::fixed to drop the
  // scientific notation (CAPD's parser is conservative about exponent forms).
  const string s = capd_str(1e-10);
  EXPECT_EQ(s.find('e'), string::npos)
      << "scientific notation slipped through: " << s;
  EXPECT_EQ(s.find('E'), string::npos);
}

TEST_F(ToCapdStringTest, ConstantLargeScientificRoundTrip) {
  const string s = capd_str(1.5e10);
  EXPECT_EQ(s.find('e'), string::npos)
      << "large scientific notation slipped through: " << s;
}

TEST_F(ToCapdStringTest, ConstantNegativeScientific) {
  // After round-trip, leading '-' must still be wrapped.
  const string s = capd_str(-2.5e-5);
  EXPECT_EQ(s.find('e'), string::npos);
  EXPECT_EQ(s.front(), '(');
  EXPECT_EQ(s.back(), ')');
}

TEST_F(ToCapdStringTest, ConstantRoundTripsExactly) {
  // Soundness regression for the 6-digit truncation bug: to_capd_string used
  // std::to_string, which renders a double with only 6 fractional digits
  // (sprintf %f). So 1/3 became "0.333333" and CAPD integrated 3*(1/3) as
  // 0.999999 — a 1e-6-unfaithful vector field that false-unsats clock ODEs
  // whose terminal gate sits at the integration-window end (the water /
  // thermostat automata; ode_soundness_repros/ws_taupin.smt2). The emitted
  // string must round-trip to the exact double so CAPD's interval-parse of the
  // decimal literal brackets the true coefficient.
  for (const double v : {1.0 / 3.0, 9.8066499999999994, 0.1, 2.0 / 7.0,
                         -1.0 / 3.0, 1234.5678901234567}) {
    const string s = capd_str(v);
    // strip the negative-wrap parens before parsing back.
    const string num =
        (s.front() == '(') ? s.substr(1, s.size() - 2) : s;
    EXPECT_EQ(std::stod(num), v)
        << "to_capd_string lost precision: " << v << " -> '" << s << "'";
  }
  EXPECT_NE(capd_str(1.0 / 3.0), "0.333333");
}

// ===========================================================================
// Variables
// ===========================================================================

TEST_F(ToCapdStringTest, VariableEmitsName) {
  const Variable x{"x_var", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(Expression{x}), "x_var");
}

// ===========================================================================
// Addition
// ===========================================================================

TEST_F(ToCapdStringTest, AdditionWithConstant) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  // 2 + x = Add(constant=2, x->1).
  const string s = capd_str(2.0 + Expression{x});
  // Form: "(2+1*x)" — outer parens, constant first.
  EXPECT_EQ(s.front(), '(');
  EXPECT_EQ(s.back(), ')');
  EXPECT_NE(s.find('2'), string::npos);
  EXPECT_NE(s.find("*x"), string::npos);
}

TEST_F(ToCapdStringTest, AdditionTwoVarsCommutativeSafe) {
  // x + y can map-iterate in either order. Just check both terms appear.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const string s = capd_str(Expression{x} + Expression{y});
  EXPECT_NE(s.find("*x"), string::npos);
  EXPECT_NE(s.find("*y"), string::npos);
  // No spurious scientific notation should appear for unit coefficients.
  EXPECT_EQ(s.find('e'), string::npos);
}

TEST_F(ToCapdStringTest, AdditionWithNegativeCoefficient) {
  // -3 * x + 1 should produce a negative-wrapped coefficient.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = capd_str(1.0 - 3.0 * Expression{x});
  // The "-3" coefficient gets wrapped: "(-3)*x"
  EXPECT_NE(s.find("(-3)*x"), string::npos)
      << "missing negative coefficient wrap in: " << s;
}

// ===========================================================================
// Multiplication
// ===========================================================================

TEST_F(ToCapdStringTest, MultiplicationConstantAndVariable) {
  // 4 * x → Mul(constant=4, x->1).
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = capd_str(4.0 * Expression{x});
  // Form: "(4*(x^1))"
  EXPECT_EQ(s.front(), '(');
  EXPECT_EQ(s.back(), ')');
  EXPECT_NE(s.find('4'), string::npos);
  EXPECT_NE(s.find("x^1"), string::npos);
}

TEST_F(ToCapdStringTest, MultiplicationTwoVarsCommutativeSafe) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const string s = capd_str(Expression{x} * Expression{y});
  EXPECT_NE(s.find("x^1"), string::npos);
  EXPECT_NE(s.find("y^1"), string::npos);
}

// ===========================================================================
// Division
// ===========================================================================

TEST_F(ToCapdStringTest, DivisionEmitsSlashedForm) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const string s = capd_str(Expression{x} / Expression{y});
  EXPECT_EQ(s, "(x/y)");
}

// ===========================================================================
// Pow
// ===========================================================================

TEST_F(ToCapdStringTest, PowIntegerExponent) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  // pow(x, 2) on dReal sometimes flows into Multiplication (x*x); use
  // the explicit `pow` builder which goes through ExpressionKind::Pow.
  const string s = capd_str(pow(Expression{x}, 3.0));
  EXPECT_NE(s.find("x^3"), string::npos)
      << "expected x^3 in: " << s;
}

// ===========================================================================
// Transcendentals
// ===========================================================================

TEST_F(ToCapdStringTest, ExpEmitsExpFunction) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(exp(Expression{x})), "exp(x)");
}

TEST_F(ToCapdStringTest, SinAndCosPassThrough) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(sin(Expression{x})), "sin(x)");
  EXPECT_EQ(capd_str(cos(Expression{x})), "cos(x)");
}

TEST_F(ToCapdStringTest, TanRewrittenAsSinDivCos) {
  // CAPD's IMap doesn't accept tan directly; converter must rewrite.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(tan(Expression{x})), "(sin(x)/cos(x))");
}

TEST_F(ToCapdStringTest, AbsRewrittenAsSqrtSqr) {
  // CAPD's IMap doesn't accept abs directly; converter must rewrite as
  // sqrt(sqr(x)). This is mathematically identical for real x.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(abs(Expression{x})), "sqrt(sqr(x))");
}

TEST_F(ToCapdStringTest, SqrtEmitsSqrt) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(sqrt(Expression{x})), "sqrt(x)");
}

TEST_F(ToCapdStringTest, LogEmitsLog) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(log(Expression{x})), "log(x)");
}

TEST_F(ToCapdStringTest, AsinAcosAtanPassThrough) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  EXPECT_EQ(capd_str(asin(Expression{x})), "asin(x)");
  EXPECT_EQ(capd_str(acos(Expression{x})), "acos(x)");
  EXPECT_EQ(capd_str(atan(Expression{x})), "atan(x)");
}

TEST_F(ToCapdStringTest, SinhRewrittenViaExp) {
  // sinh(x) = (exp(x) - exp(-x))/2 in the converter's rewrite.
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = capd_str(sinh(Expression{x}));
  EXPECT_NE(s.find("exp(x)"), string::npos);
  EXPECT_NE(s.find("exp(-x)"), string::npos);
  EXPECT_NE(s.find("- "), string::npos);  // ensures '-' separator present
  EXPECT_NE(s.find("/ 2"), string::npos);
}

TEST_F(ToCapdStringTest, CoshRewrittenViaExp) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = capd_str(cosh(Expression{x}));
  EXPECT_NE(s.find("exp(x)"), string::npos);
  EXPECT_NE(s.find("exp(-x)"), string::npos);
  EXPECT_NE(s.find("+ "), string::npos);
  EXPECT_NE(s.find("/ 2"), string::npos);
}

TEST_F(ToCapdStringTest, TanhRewrittenViaExp) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const string s = capd_str(tanh(Expression{x}));
  EXPECT_NE(s.find("exp(x)"), string::npos);
  EXPECT_NE(s.find("exp(-x)"), string::npos);
  // tanh = (e^x - e^-x) / (e^x + e^-x): both ops present.
  EXPECT_NE(s.find("-"), string::npos);
  EXPECT_NE(s.find("+"), string::npos);
}

// ===========================================================================
// Unsupported expression kinds throw
// ===========================================================================

TEST_F(ToCapdStringTest, IfThenElseThrows) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  // build an ITE expression: if x>0 then x else y
  const Expression ite_expr = if_then_else(
      Expression{x} > 0.0, Expression{x}, Expression{y});
  EXPECT_THROW({ (void)capd_str(ite_expr); }, std::runtime_error);
}

// ===========================================================================
// Composition smoke test (ODE-RHS-shaped expression).
// ===========================================================================

TEST_F(ToCapdStringTest, CompositeMockProstateRhs) {
  // dx/dt for the mock-prostate fixture: -x * (z/(z+2)).
  // This is the kind of expression that builds a multi-level Mul tree —
  // verify the converter doesn't drop terms.
  const Variable xv{"x", Variable::Type::CONTINUOUS};
  const Variable zv{"z", Variable::Type::CONTINUOUS};
  const Expression rhs =
      -Expression{xv} * (Expression{zv} / (Expression{zv} + 2.0));
  const string s = capd_str(rhs);
  // All three variables/constants must appear somewhere.
  EXPECT_NE(s.find("x^1"), string::npos);
  // z appears twice in the RHS; at least one occurrence must survive.
  EXPECT_NE(s.find("z"), string::npos);
  EXPECT_NE(s.find('2'), string::npos);
  // No 'e' in scientific notation.
  EXPECT_EQ(s.find('e'), s.find("exp"))  // 'e' may legally appear inside "exp"
      << "unexpected scientific notation in: " << s;
}

}  // namespace
}  // namespace dreal
