/*
   Copyright 2017 Toyota Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include <exception>
#include <string>

#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/symbolic/symbolic.h"

// H2 (robustness): dReal supports only a top-level POSITIVE ∃∀ `forall`. A
// negated forall (`¬∀`, which the SAT layer can also produce by assigning a
// forall-Boolean to false), a nested forall (`∀∀`), or a forall buried under a
// sign-flipping disjunction currently crashes with an OPAQUE error deep in
// contractor construction (`ibex_converter`/`DeltaStrengthen`'s VisitForall
// throw). The fix rejects these EARLY at Context::Assert with a clear, actionable
// message. These tests pin that the rejection message is the specific one — they
// are RED before the fix (the pre-fix throw, if any, carries a different/opaque
// message) and GREEN after.

namespace dreal {
namespace {

// The distinctive phrase the early rejection must contain.
constexpr const char* kRejectPhrase = "top-level positive forall";

class ForallUnsupportedRejectionTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};  // existential
  const Variable t_{"t", Variable::Type::CONTINUOUS};  // universal
  const Variable s_{"s", Variable::Type::CONTINUOUS};  // universal (inner)

  // Runs CheckSatisfiability and asserts it throws with the rejection phrase.
  void ExpectRejected(const Formula& f) {
    try {
      CheckSatisfiability(f, 0.001);
      FAIL() << "expected an early rejection, but solving returned a verdict";
    } catch (const std::exception& e) {
      EXPECT_NE(std::string{e.what()}.find(kRejectPhrase), std::string::npos)
          << "rejection must be the clear top-level-positive-forall message; got: "
          << e.what();
    }
  }
};

// ¬(∀t. a·t ≤ -0.1) — a negated forall asserted at top level.
TEST_F(ForallUnsupportedRejectionTest, NegatedForallRejected) {
  const Formula inner{forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                         a_ * t_ <= -0.1))};
  ExpectRejected((-1.0 <= a_) && (a_ <= 1.0) && !inner);
}

// ∀t. ∀s. (…) — a nested forall (the body itself quantifies).
TEST_F(ForallUnsupportedRejectionTest, NestedForallRejected) {
  const Formula nested{forall(
      {t_}, forall({s_}, imply((t_ >= 0.0) && (t_ <= 1.0) && (s_ >= 0.0) &&
                                   (s_ <= 1.0),
                               a_ + t_ + s_ >= 0.0)))};
  ExpectRejected((-1.0 <= a_) && (a_ <= 1.0) && nested);
}

// (b ∨ ∀t. …) — a forall under a disjunction; the SAT layer can assign the
// forall-Boolean to false, flipping its sign into the unsupported region.
TEST_F(ForallUnsupportedRejectionTest, DisjoinedForallRejected) {
  const Variable b_{"b", Variable::Type::CONTINUOUS};
  const Formula inner{forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                         a_ * t_ <= -0.1))};
  ExpectRejected((-1.0 <= a_) && (a_ <= 1.0) && ((b_ >= 5.0) || inner));
}

}  // namespace
}  // namespace dreal
