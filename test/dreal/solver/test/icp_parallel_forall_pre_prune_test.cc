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

// IcpParallel (--jobs > 1) must drive the --forall-pre-prune pre-pruner: each
// worker owns its own ContractorIbexForall (the inner ibex::CtcForAll holds a
// mutable parameter-box worklist, so it cannot be shared). The pre-pruner only
// contracts the existential box — variable/box contraction never moves a
// verdict — so the parallel-pre-prune verdict must match the sequential
// (jobs=1) baseline, both with and without the pre-pruner.
//
// Red-first artifact (the test-first obligation): before the ContractorIbexForallMt
// wiring, the factory `make_contractor_ibex_forall` THREW
// "The forall pre-pruner (--forall-pre-prune) is not implemented for parallel
// ICP (--jobs > 1)." at contractor-build time, so every CheckSatisfiability call
// below threw std::runtime_error and the EXPECT_TRUE/FALSE assertions failed.
// After the wiring the throw is gone and the verdicts must hold, repeatedly and
// without racing on the per-worker ibex state (parallel branch order is
// nondeterministic but the verdict is not).

namespace dreal {
namespace {

class IcpParallelForallPrePruneTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};  // existential
  const Variable t_{"t", Variable::Type::CONTINUOUS};  // universal (∀-bound)

  // jobs > 1 -> IcpParallel; forall-pre-prune on -> the ContractorIbexForall
  // pre-pruner is built beside the CEGIS ContractorForall.
  Config ParallelPrePrune() const {
    Config config;
    config.mutable_precision() = 0.001;
    config.mutable_number_of_jobs() = 4;
    config.mutable_use_forall_pre_prune() = true;
    return config;
  }
};

// δ-SAT: ∃a∈[-1,1] ∀t∈[0,1]. (t² ≥ 0.25) ⟹ (a·t ≤ -0.1). The guard makes
// t<0.5 vacuous, so a=-0.5 is a true witness (same instance as the
// contractor-level GuardedDescentContractsButKeepsWitness). Parallel pre-prune
// must keep that witness — agree with the sequential baseline, repeatedly.
TEST_F(IcpParallelForallPrePruneTest, SatMatchesSequentialUnderRepeat) {
  const Formula domain{(t_ >= 0.0) && (t_ <= 1.0)};
  const Formula body{imply(pow(t_, 2.0) >= 0.25, a_ * t_ <= -0.1)};
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) &&
                  forall({t_}, imply(domain, body))};

  ASSERT_TRUE(CheckSatisfiability(f, 0.001))
      << "sequential baseline expected δ-SAT (a=-0.5 is a witness)";
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(CheckSatisfiability(f, ParallelPrePrune()))
        << "parallel pre-prune must be δ-SAT (iteration " << i << ")";
  }
}

// δ-UNSAT: ∃a∈[1,2] ∀t∈[0,1]. a·t ≤ -0.1. At t=1 this forces a ≤ -0.1, which no
// a∈[1,2] satisfies, so there is no existential witness. The pre-pruner empties
// the existential box; parallel workers must join an empty box and agree with
// the sequential refutation, repeatedly.
TEST_F(IcpParallelForallPrePruneTest, UnsatMatchesSequentialUnderRepeat) {
  const Formula domain{(t_ >= 0.0) && (t_ <= 1.0)};
  const Formula f{(1.0 <= a_) && (a_ <= 2.0) &&
                  forall({t_}, imply(domain, a_ * t_ <= -0.1))};

  ASSERT_FALSE(CheckSatisfiability(f, 0.001))
      << "sequential baseline expected UNSAT (no a∈[1,2] satisfies ∀t)";
  for (int i = 0; i < 20; ++i) {
    EXPECT_FALSE(CheckSatisfiability(f, ParallelPrePrune()))
        << "parallel pre-prune must be UNSAT (iteration " << i << ")";
  }
}

}  // namespace
}  // namespace dreal
