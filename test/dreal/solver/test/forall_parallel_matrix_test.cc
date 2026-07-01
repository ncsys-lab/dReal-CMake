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
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"

// Combinatorial net for the two under-maintained features in combination:
// `forall` (∃∀ CEGIS) and parallel ICP. None of these knobs — job count, the
// --forall-pre-prune pre-pruner — may move a verdict (variable/box contraction
// and worker count are verdict-neutral). We assert verdict EQUALITY across the
// full matrix {jobs 1,2,4} × {pre-prune off,on} vs the sequential CEGIS baseline,
// repeated to shake out races (parallel branch order is nondeterministic; the
// verdict is not). Critically this includes jobs>1 WITHOUT the pre-pruner — the
// ContractorForallMt CEGIS-only parallel path, which had no prior test.

namespace dreal {
namespace {

class ForallParallelMatrixTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};
  const Variable b_{"b", Variable::Type::CONTINUOUS};
  const Variable t_{"t", Variable::Type::CONTINUOUS};

  static Config Cfg(int jobs, bool pre_prune) {
    Config config;
    config.mutable_precision() = 0.001;
    config.mutable_number_of_jobs() = jobs;
    config.mutable_use_forall_pre_prune() = pre_prune;
    return config;
  }

  // Asserts that @p f solves to @p expect_sat across every (jobs, pre-prune)
  // combination, repeatedly.
  void ExpectInvariantVerdict(const Formula& f, bool expect_sat) {
    // Sequential CEGIS baseline (jobs=1, no pre-prune).
    ASSERT_EQ(static_cast<bool>(CheckSatisfiability(f, Cfg(1, false))),
              expect_sat)
        << "baseline verdict mismatch";
    for (const int jobs : {1, 2, 4}) {
      for (const bool pre_prune : {false, true}) {
        for (int rep = 0; rep < 6; ++rep) {
          EXPECT_EQ(
              static_cast<bool>(CheckSatisfiability(f, Cfg(jobs, pre_prune))),
              expect_sat)
              << "verdict moved at jobs=" << jobs
              << " pre_prune=" << pre_prune << " rep=" << rep;
        }
      }
    }
  }
};

// δ-SAT: ∃a∈[-1,1] ∀t∈[0,1]. (t²≥0.25) ⟹ (a·t ≤ −0.1). a=−0.5 is a witness.
TEST_F(ForallParallelMatrixTest, GuardedSat) {
  const Formula body{imply(pow(t_, 2.0) >= 0.25, a_ * t_ <= -0.1)};
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0), body))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

// UNSAT: ∃a∈[1,2] ∀t∈[0,1]. a·t ≤ −0.1. At t=1 this needs a ≤ −0.1; no a∈[1,2].
TEST_F(ForallParallelMatrixTest, GuardedUnsat) {
  const Formula f{(1.0 <= a_) && (a_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                     a_ * t_ <= -0.1))};
  ExpectInvariantVerdict(f, /*expect_sat=*/false);
}

// δ-SAT, transcendental: ∃a∈[0,2] ∀t∈[0,π]. a ≥ sin(t). a≥1 works.
TEST_F(ForallParallelMatrixTest, TranscendentalSat) {
  const Formula f{(0.0 <= a_) && (a_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 3.14159265),
                                     a_ >= sin(t_)))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

// δ-SAT, two existential variables: ∃a∈[-1,1],b∈[0.5,2] ∀t∈[0,1]. a·t + b ≥ 0.
TEST_F(ForallParallelMatrixTest, MultiExistentialSat) {
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) && (0.5 <= b_) && (b_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                     a_ * t_ + b_ >= 0.0))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

}  // namespace
}  // namespace dreal
