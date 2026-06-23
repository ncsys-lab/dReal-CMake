// End-to-end SMT2-path regression guards for the dReal bugs catalogued in
// simulink-to-dreal/docs/dreal-bugs.md. Each test embeds the *exact* reproducer
// text committed there (`bug00N_*.smt2`) and pins the correct verdict / model,
// so a regression on any of these resurfaces here rather than silently in the
// downstream translator.
//
// Covered here (the verdict + --model bugs):
//   BUG-001  nested-arithmetic define-ode RHS                -> delta-sat (not unsat)
//   BUG-003  ZOH-parameter coupled ODE under (integral …)    -> delta-sat (not unsat)
//   BUG-005  --model value of the (integral …) endpoint var  -> accurate (not scrambled)
//   BUG-006  unsat (integral …) formula                      -> unsat (not false delta-sat)
//   BUG-008  endpoint asserted below its true value          -> unsat (not false delta-sat)
//
// BUG-002 is intentionally excluded (negated (integral …) is silently dropped
// by design in every dReal version — a documented §6 semantics choice, not a
// fixable defect). The --visualize bugs (BUG-004 empty JSON, BUG-007 step
// field) are exercised at the generate_trace level in contractor_capd_test.cc,
// since the --visualize file-write path is not reachable through parse_string.

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// Parse an SMT2 string (its (check-sat) prints the verdict to std::cout, and
// the model too when produce_models is set) and return the captured output.
// Default Config precision is 0.001 — the delta the bug doc's reproducers use.
std::string RunSmt2String(const std::string& smt2, bool produce_models = false) {
  Config config;
  if (produce_models) config.mutable_produce_models().set_from_command_line(true);
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  std::streambuf* const old{std::cout.rdbuf(captured.rdbuf())};
  driver.parse_string(smt2);
  std::cout.rdbuf(old);
  return captured.str();
}

// BUG-001 — a define-ode RHS with a nested arithmetic subexpression
// (* (- (/ 1.0 10.0)) x) was a cav26 regression that false-`unsat`'d. The
// equivalent literal-coefficient form (* -0.1 x) was always correct. x decays
// from 100, so x_t ≈ 90.5 is reachable: delta-sat.
TEST(DrealBugsRegression, Bug001_NestedArithRhs_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* (- (/ 1.0 10.0)) x))))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (>= x_t 0)\n"
      "  (<= x_t 100)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-003 — a ZOH-parameter ODE (d/dt[y]=0) coupled into another state
// (d/dt[x]=y) under an (integral …) assertion was a cav26 regression that
// false-`unsat`'d. x(t)=20+30·t so x(1)=50 ≥ 49: delta-sat.
TEST(DrealBugsRegression, Bug003_ZohParameterCoupling_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun y () Real [0.000000, 50.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun y_0 () Real [0.000000, 50.000000])\n"
      "(declare-fun y_t () Real [0.000000, 50.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] y) (= d/dt[y] 0)))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0 20)\n"
      "  (= y_0 30)\n"
      "  (= [x_t y_t] (integral 0. time [x_0 y_0] flow_1))\n"
      "  (>= x_t 49)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-005 — the --model value of the (integral …) endpoint variable was
// scrambled/mis-printed (cav26 fully scrambled; pre-fix popl27 printed 61.597).
// True x_0_t = 100·e^−0.5 ≈ 60.6531. Assert the printed endpoint box contains
// the true value and excludes the bug's 61.597. (Same defect locus as BUG-008,
// also covered by PinnedDecayTest in contractor_odes_semantic_test.cc.)
TEST(DrealBugsRegression, Bug005_ModelEndpointValue_Accurate) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time_0 () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* -0.5 x))))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0_0 100)\n"
      "  (= [x_0_t] (integral 0. time_0 [x_0_0] flow_1))\n"
      "  (>= x_0_t 0)\n"
      "  (<= x_0_t 100)\n"
      "))\n"
      "(check-sat)\n",
      /*produce_models=*/true)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  // The model line for x_0_t must report a box around 60.6531, not the bug's
  // 61.597 (off by ~1, outside the printed delta-width box).
  const std::size_t pos{out.find("x_0_t")};
  ASSERT_NE(pos, std::string::npos) << "no x_0_t in model; got: " << out;
  const std::string line{out.substr(pos, out.find('\n', pos) - pos)};
  EXPECT_NE(line.find("60.653"), std::string::npos)
      << "endpoint must print x(1)≈60.6531, not the scrambled/61.597 value: "
      << line;
}

// BUG-006 — an unsat (integral …) formula (x decays to 60.65, asserted < 10;
// gap ≫ δ) false-`delta-sat`'d on early v5 builds. Must be unsat.
TEST(DrealBugsRegression, Bug006_UnsatIntegral_Unsat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* -0.5 x))))\n"
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (< x_t 10)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-008 — the (integral …) endpoint asserted below its true value
// (x(1)=100−80·e^−0.5≈51.4775, asserted ≤ 51.0; gap 0.477 ≫ δ) was false-
// `delta-sat` on pre-fix popl27 (endpoint var under-contracted). Must be unsat.
// This is bug005_contract_trigger.smt2; also covered by PinnedRiseTest.
TEST(DrealBugsRegression, Bug008_SubTrueEndpoint_Unsat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* 0.500000 (- 100.000000 x)))))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0 20)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (<= x_t 51.0)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

}  // namespace
}  // namespace dreal
