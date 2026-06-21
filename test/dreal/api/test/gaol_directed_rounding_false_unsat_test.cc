// Regression test for a false-UNSAT soundness bug on the `upgrade-ibex`
// branch (IBEX source-built from ncsys-lab/ibex-lib@dreal-perf-patches).
//
// SYMPTOM (end-to-end): the solver reports `unsat` for trivially-satisfiable
// queries whose only continuous content is an inequality with an
// inexactly-FP-representable product on one side and a *pinned* variable, e.g.
//
//     (assert (= x 0.4))
//     (assert (<= (* 0.1 x) 1.0))   ;; 0.04 <= 1  — obviously SAT
//
// dReal4 (this branch) returns UNSAT; dreal4_cav26 returns delta-sat. This was
// the root cause of the false-UNSATs on the 1mhz_*_saradc_*_box_* benchmarks
// (GT = SAT): those formulas are saturated with `(* 3.3 x)` and `(* C (pow 10
// -N))` style terms inline in inequalities with pinned operands.
//
// ROOT CAUSE (gaol directed rounding, NOT the HC4 backward-callback patch):
// IBEX's gaol interval backend requires the FPU rounding mode to be FE_UPWARD
// for sound directed rounding. Verified at the pure-IBEX level: the HC4
// backward of `(0.1*0.1) - y <= 0` over y=[1000,1000] keeps the box non-empty
// under FE_UPWARD but EMPTIES it (lo>hi inverted interval) under FE_TONEAREST /
// FE_DOWNWARD / FE_TOWARDZERO — identically for the 2-arg and the 3-arg
// callback `Function::backward`, so the callback/Domain patch is not at fault.
// The minimal `dreal-perf-patches` fork dropped the cav26-era gaol/FPU-init
// patches (059d1fe7 "Call gaol::init from wrapper", fc986657 / 4f845aa3 FPU
// management for Apple Silicon); see the companion ibex_gaol_init_smoke_test.cc
// and ibex_fpu_platform_smoke_test.cc, which already capture `Interval(0.1) +
// Interval(0.2)` collapsing to a singleton on this build.
//
// NOTE on test shape: the trigger must reach the ibex/gaol multiply at HC4
// eval time. A *constant*-only product (`0.1 * 0.1`) is folded to a single
// double by Drake's symbolic layer when built through the C++ API, so it does
// NOT reproduce here (it only survives unfolded through the SMT2 *parser*). A
// `constant * variable` product is not folded and is the faithful API trigger.
//
// FIXED by establishing FE_UPWARD per Prune in
// src/dreal/contractor/contractor_ibex_fwdbwd.cc. Both tests below now pass;
// the first one fails (false UNSAT) if that guard is removed.

#include "dreal/api/api.h"

#include <gtest/gtest.h>

#include "dreal/symbolic/symbolic.h"

namespace dreal {
namespace {

constexpr double kDelta = 0.001;

// FAITHFUL REPRODUCER (regresses to false UNSAT without the FE_UPWARD guard).
//   (0.1 * x) <= 1  &&  x == 0.4   ->  0.04 <= 1  ->  SAT.
// 0.1 is not exactly FP-representable, so gaol's interval multiply needs
// sound directed rounding; with the FPU mis-configured it inverts to an empty
// interval and the box is wrongly emptied.
TEST(GaolDirectedRoundingFalseUnsat, VarTimesInexactConstantLeqBound) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Formula f{(0.1 * x <= 1.0) && (x == 0.4)};
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "0.04 <= 1 is satisfiable; UNSAT here is the gaol "
         "directed-rounding false-UNSAT.";
}

// CONTROL (must always pass): an exactly-FP-representable product needs no
// rounding, so it is unaffected by the directed-rounding bug.
//   (0.5 * x) <= 1  &&  x == 0.5   ->  0.25 <= 1  ->  SAT.
TEST(GaolDirectedRoundingFalseUnsat, ExactProductControlStaysSat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Formula f{(0.5 * x <= 1.0) && (x == 0.5)};
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "0.25 <= 1 with an exactly-representable coefficient must stay SAT.";
}

// === End-to-end net for dreal/dreal4#321 constant-fold underflow ============
// A constant pow/mul/div of exactly-representable literals (Constant kind, what
// the parser builds for 0.5 / integers) whose result underflows would fold to
// the lying scalar 0.0, making `> 0` a false unsat. sound_constant_fold
// (symbolic_expression.cc) folds to a sound RealConstant interval instead, so
// these stay delta-SAT. The variabled case below is the one that previously
// produced a malformed ExpressionMul (the symbolic-Pow attempt) -- folding to a
// RealConstant avoids it. The actual parser path is DenormUnderflowSmt2.
TEST(DenormUnderflowEndToEnd, PowUnderflowSat) {
  const Formula f{pow(Expression{0.5}, Expression{1075.0}) > 0};
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "pow(0.5,1075) > 0 must be delta-SAT; folding to 0.0 is a false UNSAT.";
}

TEST(DenormUnderflowEndToEnd, PowUnderflowVariabledSat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Formula f{(x == pow(Expression{0.5}, Expression{1075.0})) && (x > 0)};
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "x = pow(0.5,1075) & x > 0 must be delta-SAT.";
}

TEST(DenormUnderflowEndToEnd, DivUnderflowSat) {
  const Formula f{(Expression{1e-320} / Expression{1e10}) > 0};  // 1e-330 -> 0
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "1e-320/1e10 > 0 must be delta-SAT.";
}

TEST(DenormUnderflowEndToEnd, MulUnderflowSat) {
  const Formula f{(Expression{1e-200} * Expression{1e-150}) > 0};  // 1e-350 -> 0
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "1e-200 * 1e-150 > 0 must be delta-SAT.";
}

// CONTROL: a genuinely-false query stays UNSAT (no spurious SAT from the bracket).
TEST(DenormUnderflowEndToEnd, FaithfulControlUnsat) {
  const Formula f{pow(Expression{2.0}, Expression{3.0}) < 0};  // 8 < 0
  EXPECT_FALSE(CheckSatisfiability(f, kDelta))
      << "pow(2,3) = 8; 8 < 0 must stay UNSAT.";
}

}  // namespace
}  // namespace dreal
