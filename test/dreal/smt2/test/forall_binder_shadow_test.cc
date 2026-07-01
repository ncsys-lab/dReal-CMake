// SMT2-path guard for QUIRK-001 (ode_expressivity_energy/docs/dreal-bugs.md):
// a `forall`-bound variable whose name COLLIDES with a top-level declared (model)
// variable was silently mis-solved. The two `x`s are distinct Variables that both
// print as "x"; the top-level `x` is left unconstrained (full real line) and driven
// to an absurd value, so a query whose single-variable reading is `unsat` returns a
// spurious `delta-sat`. There is no warning — a silent wrong verdict.
//
// DESIRED (implemented here): reject the collision at parse time. The guard fires
// ONLY when the shadowed name is a top-level model variable (one declared via
// declare-const/declare-fun); a benign `forall`-over-`forall` shadow with no
// top-level variable of that name is untouched (InlineForallNoCollisionSolves).
//
// These drive the full SMT2 path (parse -> Context), like dreal_bugs_regression_test.cc.

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// RAII redirect of std::cout, restored on BOTH the normal and the exception paths.
// Required because parse_string throws on the shadowed query: a plain restore-after
// would be skipped during unwinding, leaving std::cout pointing at a destroyed buffer.
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

// Parse an SMT2 string (its (check-sat) prints the verdict to std::cout) and return
// the captured output. The query sets its own precision via (set-option :precision …).
std::string RunSmt2String(const std::string& smt2) {
  Config config;
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  const CoutRedirect redirect{captured.rdbuf()};
  driver.parse_string(smt2);
  return captured.str();
}

// The exact quirk001_trigger_shadowed_cse.smt2: x^2 hoisted to a top-level `cse`
// tied to a top-level (free) `x` that shadows the forall-bound `x`. Pre-fix this
// returned delta-sat (cse=1.2e308, x=-1.1e154); DESIRED: a parse-time rejection.
const char* const kShadowedForall =
    "(set-option :precision 0.000500000000000000)\n"
    "(declare-const cse Real)\n"
    "(declare-const x Real)\n"
    "(assert (= cse (* x x)))\n"
    "(assert (forall ((x Real [-1, 1])) (>= cse 1)))\n"
    "(check-sat )\n"
    "(exit)\n";

// The exact quirk001_baseline_inline_forall.smt2: the bound x's subterm kept INSIDE
// the forall, and NO top-level `x`. ∀x∈[-1,1]. x^2 >= 1 is false (x=0), so: unsat.
const char* const kInlineForall =
    "(set-option :precision 0.000500000000000000)\n"
    "(assert (forall ((x Real [-1, 1])) (>= (* x x) 1)))\n"
    "(check-sat )\n"
    "(exit)\n";

TEST(ForallBinderShadow, ShadowedForallBinderThrows) {
  try {
    const std::string out{RunSmt2String(kShadowedForall)};
    FAIL() << "expected a parse-time rejection of the forall binder shadowing the "
              "top-level declared `x`, but got: "
           << out;
  } catch (const std::runtime_error& e) {
    const std::string msg{e.what()};
    EXPECT_NE(msg.find("shadow"), std::string::npos) << "message: " << msg;
    EXPECT_NE(msg.find('x'), std::string::npos) << "message: " << msg;
  }
}

TEST(ForallBinderShadow, InlineForallNoCollisionSolves) {
  const std::string out{RunSmt2String(kInlineForall)};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

}  // namespace
}  // namespace dreal
