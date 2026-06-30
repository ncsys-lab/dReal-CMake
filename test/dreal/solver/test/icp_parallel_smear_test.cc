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
#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/solver/config.h"  // SmearVariant
#include "dreal/symbolic/symbolic.h"

// IcpParallel (--jobs > 1) must drive --smear branching: each worker owns its
// own SmearBrancher (operator() mutates ibex Function eval scratch, so it cannot
// be shared). Variable choice never moves a verdict, so the parallel-smear
// verdict must match the sequential (jobs=1, largest-first) baseline.
//
// This is a soundness/threading REGRESSION GUARD, not a red-first functional
// test: with smear off, IcpParallel already returns the correct verdict via
// largest-first, so a verdict assertion is green before and after the wiring.
// The genuine red-first artifact is the CLI guard removal — the binary threw
// "--smear is not implemented for parallel ICP (--jobs > 1)" before this change.
// The repeat loop shakes out data races on the per-worker ibex state (verdicts
// are deterministic even though parallel branch order is not).

namespace dreal {
namespace {

class IcpParallelSmearTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};

  // jobs > 1 -> IcpParallel; kSum -> smear active on the relational system.
  Config ParallelSmear() const {
    Config config;
    config.mutable_precision() = 0.001;
    config.mutable_number_of_jobs() = 4;
    config.mutable_smear_variant() = SmearVariant::kSum;
    return config;
  }
};

// δ-SAT nonlinear instance that requires branching (circle ∩ hyperbola, e.g.
// x=3,y=4): parallel-smear must agree with the sequential baseline, repeatedly
// and without racing.
TEST_F(IcpParallelSmearTest, SatMatchesSequentialUnderRepeat) {
  const Formula f{0 <= x_ && x_ <= 10 && 0 <= y_ && y_ <= 10 &&
                  x_ * x_ + y_ * y_ == 25 && x_ * y_ >= 6};
  ASSERT_TRUE(CheckSatisfiability(f, 0.001))
      << "sequential baseline expected δ-SAT";
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(CheckSatisfiability(f, ParallelSmear()))
        << "parallel-smear must be δ-SAT (iteration " << i << ")";
  }
}

// δ-UNSAT nonlinear instance: the radius-1 circle (x,y ∈ [0,1]) cannot meet
// x+y >= 5. Parallel workers join an empty box; smear branching must not change
// the refutation.
TEST_F(IcpParallelSmearTest, UnsatMatchesSequentialUnderRepeat) {
  const Formula f{0 <= x_ && x_ <= 10 && 0 <= y_ && y_ <= 10 &&
                  x_ * x_ + y_ * y_ == 1 && x_ + y_ >= 5};
  ASSERT_FALSE(CheckSatisfiability(f, 0.001))
      << "sequential baseline expected UNSAT";
  for (int i = 0; i < 20; ++i) {
    EXPECT_FALSE(CheckSatisfiability(f, ParallelSmear()))
        << "parallel-smear must be UNSAT (iteration " << i << ")";
  }
}

}  // namespace
}  // namespace dreal
