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
#include "dreal/contractor/contractor_ibex_forall.h"

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/rounding.h"

namespace dreal {
namespace {

class ContractorIbexForallTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};  // existential
  const Variable t_{"t", Variable::Type::CONTINUOUS};  // universal
};

// ∀t∈[0,1]. (t² ≥ 0.25) ⟹ (a·t ≤ -0.1).
//
// Guard region is t∈[0.5,1] (t² ≥ 0.25 ⇔ t ≥ 0.5 on [0,1]); the valid
// existential region is a ≤ -0.2 (the tightest bound, from t=0.5). The
// implication guard is *load-bearing*: with it, t<0.5 is vacuous and a=-0.5 is
// a true witness; drop it and t→0 forces a·0 = 0 ≤ -0.1, emptying the box (a
// false `unsat`, a SOUNDNESS bug). This test therefore guards both that the
// pre-pruner contracts AND that the CtcUnion(¬guard, …) implication keeps a
// genuine ∃∀ witness — the soundness crux.
TEST_F(ContractorIbexForallTest, GuardedDescentContractsButKeepsWitness) {
  const Formula domain{(t_ >= 0.0) && (t_ <= 1.0)};
  const Formula body{imply(pow(t_, 2.0) >= 0.25, a_ * t_ <= -0.1)};
  const Formula f{forall({t_}, imply(domain, body))};

  Box box{{a_}};
  box[a_] = Box::Interval(-1.0, 1.0);
  ContractorStatus cs{box};
  const ContractorIbexForall ctc{f, box, Config{}};
  ASSERT_FALSE(ctc.is_dummy());

  EXPECT_FALSE(cs.box().empty());
  {
    const UpwardRoundingScope rms;
    ctc.Prune(&cs, rms.token());
  }

  // (a) Contraction: a's upper bound is pulled down from 1.0 to ≈ -0.2.
  EXPECT_FALSE(cs.box().empty())
      << "the guard makes the query satisfiable (e.g. a=-0.5); must not empty.";
  EXPECT_LE(cs.box()[a_].ub(), -0.15)
      << "∀t∈[0.5,1]: a·t≤-0.1 ⇒ a≤-0.2; the upper bound must be contracted.";

  // (b) Soundness: a=-0.5 (≤ -0.2) is a real ∃∀ witness and must survive — the
  // implication guard must keep t<0.5 vacuous. Dropping the guard empties here.
  EXPECT_TRUE(cs.box()[a_].contains(-0.5))
      << "a=-0.5 is a true ∃∀ witness; the pre-pruner must not delete it.";
}

// ∀t∈[0,0.4]. (t² ≥ 0.25) ⟹ (a·t ≤ -0.1). The guard t² ≥ 0.25 ⇔ t ≥ 0.5 never
// holds on [0,0.4], so the body is vacuously true and a must NOT be pruned.
TEST_F(ContractorIbexForallTest, VacuousWhenGuardNeverHolds) {
  const Formula domain{(t_ >= 0.0) && (t_ <= 0.4)};
  const Formula body{imply(pow(t_, 2.0) >= 0.25, a_ * t_ <= -0.1)};
  const Formula f{forall({t_}, imply(domain, body))};

  Box box{{a_}};
  box[a_] = Box::Interval(-1.0, 1.0);
  ContractorStatus cs{box};
  const ContractorIbexForall ctc{f, box, Config{}};
  ASSERT_FALSE(ctc.is_dummy());

  {
    const UpwardRoundingScope rms;
    ctc.Prune(&cs, rms.token());
  }
  EXPECT_TRUE(cs.box()[a_].is_superset(Box::Interval(-1.0, 1.0)))
      << "guard never holds on [0,0.4]; the forall is vacuous — no pruning.";
}

}  // namespace
}  // namespace dreal
