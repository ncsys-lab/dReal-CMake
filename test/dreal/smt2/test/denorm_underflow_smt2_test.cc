// End-to-end *parser-path* regression for the constant-underflow facet of
// dreal/dreal4#321. Parses real SMT2 text and checks the verdict, pinning the
// actual solver-input path (literals -> RealConstant, no eager fold), not just
// the equivalent C++ AST. `2^-1075` (`(^ 0.5 1075)`) underflows to 0.0 as a
// scalar, but the parser keeps it symbolic and interval evaluation brackets it
// as [0, DBL_TRUE_MIN], so `> 0` is delta-SAT rather than a false `unsat`.

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// Parse an SMT2 string (its (check-sat) prints the verdict to std::cout) and
// return the captured output. Default Config precision is 0.001.
std::string RunSmt2String(const std::string& smt2) {
  Smt2Driver driver{Context{Config{}}};
  std::ostringstream captured;
  std::streambuf* const old{std::cout.rdbuf(captured.rdbuf())};
  driver.parse_string(smt2);
  std::cout.rdbuf(old);
  return captured.str();
}

TEST(DenormUnderflowSmt2, PowUnderflowGroundSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n(assert (> (^ 0.5 1075) 0))\n(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("unsat"), std::string::npos) << "got: " << out;
}

TEST(DenormUnderflowSmt2, PowUnderflowVariabledSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n(declare-fun x () Real)\n"
      "(assert (= x (^ 0.5 1075)))\n(assert (> x 0))\n(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("unsat"), std::string::npos) << "got: " << out;
}

// CONTROL: a genuinely-false ground query stays unsat.
TEST(DenormUnderflowSmt2, FaithfulControlUnsat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n(assert (< (^ 2 3) 0))\n(check-sat)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
}

}  // namespace
}  // namespace dreal
