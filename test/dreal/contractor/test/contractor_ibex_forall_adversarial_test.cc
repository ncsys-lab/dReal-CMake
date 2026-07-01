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

// Adversarial tests for the --forall-pre-prune pre-pruner (ContractorIbexForall,
// an ibex::CtcForAll wrapper) across diverse ∃∀ shapes. They pin the TWO
// soundness invariants the feature rests on:
//
//   (1) ⊆ : Prune only ever INTERSECTS the existential box — it never widens any
//       dimension. (A widening would be the mechanism by which a pre-pruner could
//       reintroduce or fail to exclude... but more importantly, it would break the
//       fixpoint contract.)
//   (2) witness survival : a genuine ∃∀ model point is NEVER pruned away. This is
//       the SOUNDNESS crux — deleting a true model is exactly a false `unsat`
//       (asserts φ T-unsatisfiable on a T-satisfiable φ).
//
// If either invariant fails on any instance, the pre-pruner is unsound.

namespace dreal {
namespace {

class ContractorIbexForallAdversarialTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};
  const Variable b_{"b", Variable::Type::CONTINUOUS};
  const Variable t_{"t", Variable::Type::CONTINUOUS};
  const Variable s_{"s", Variable::Type::CONTINUOUS};

  // Prunes @p box under @p f, then asserts the ⊆ invariant against @p before and
  // (unless the box emptied) that each listed witness coordinate survives.
  // ContractorStatus copies @p box, and ContractorIbexForall reads it for
  // sizing, so @p box itself is the unmutated "before" snapshot.
  void PruneExpectSubsetAndWitness(
      const Formula& f, const Box& box,
      const std::vector<std::pair<Variable, double>>& witness) {
    ContractorStatus cs{box};
    const ContractorIbexForall ctc{f, box, Config{}};
    ASSERT_FALSE(ctc.is_dummy()) << "instance should build a real pre-pruner";
    {
      const UpwardRoundingScope rms;
      ctc.Prune(&cs, rms.token());
    }
    for (const Variable& v : box.variables()) {
      EXPECT_TRUE(cs.box()[v].is_subset(box[v]))
          << "⊆ invariant violated on " << v << ": " << cs.box()[v]
          << " ⊄ " << box[v];
    }
    if (!cs.box().empty()) {
      for (const auto& [v, value] : witness) {
        EXPECT_TRUE(cs.box()[v].contains(value))
            << "true ∃∀ witness " << v << "=" << value
            << " was pruned away (a false `unsat` — SOUNDNESS): " << cs.box()[v];
      }
    }
  }

  static Box MakeBox(
      const std::vector<std::pair<Variable, Box::Interval>>& dims) {
    std::vector<Variable> vars;
    vars.reserve(dims.size());
    for (const auto& [v, iv] : dims) vars.push_back(v);
    Box box{vars};
    for (const auto& [v, iv] : dims) box[v] = iv;
    return box;
  }
};

// Transcendental body: ∀t∈[0,π]. a ≥ sin(t). sup sin = 1, so a = 1.5 is a real
// witness; the pre-pruner pulls a's lower bound up toward 1 but must keep 1.5.
TEST_F(ContractorIbexForallAdversarialTest, TranscendentalWitnessSurvives) {
  const Formula f{forall(
      {t_}, imply((t_ >= 0.0) && (t_ <= 3.14159265), a_ >= sin(t_)))};
  PruneExpectSubsetAndWitness(f, MakeBox({{a_, Box::Interval(-2.0, 2.0)}}),
                              {{a_, 1.5}});
}

// Two universal variables: ∀t,s∈[0,1]². a ≥ t + s. sup(t+s) = 2 ⇒ a = 2.5 is a
// witness.
TEST_F(ContractorIbexForallAdversarialTest, MultiUniversalWitnessSurvives) {
  const Formula domain{(t_ >= 0.0) && (t_ <= 1.0) && (s_ >= 0.0) &&
                       (s_ <= 1.0)};
  const Formula f{forall({t_, s_}, imply(domain, a_ >= t_ + s_))};
  PruneExpectSubsetAndWitness(f, MakeBox({{a_, Box::Interval(-3.0, 3.0)}}),
                              {{a_, 2.5}});
}

// Two existential variables: ∃a,b ∀t∈[0,1]. a·t + b ≥ 0. a=0, b=1 is a witness
// (the line stays non-negative). Both existential dims must keep the witness.
TEST_F(ContractorIbexForallAdversarialTest, MultiExistentialWitnessSurvives) {
  const Formula f{forall(
      {t_}, imply((t_ >= 0.0) && (t_ <= 1.0), a_ * t_ + b_ >= 0.0))};
  PruneExpectSubsetAndWitness(
      f, MakeBox({{a_, Box::Interval(-1.0, 1.0)}, {b_, Box::Interval(0.5, 2.0)}}),
      {{a_, 0.0}, {b_, 1.0}});
}

// Disjunctive body: ∀t∈[0,1]. (a ≥ t) ∨ (a ≤ t − 2). a = 1.5 satisfies the first
// disjunct for all t∈[0,1]; the CtcUnion must not over-contract and delete it.
TEST_F(ContractorIbexForallAdversarialTest, DisjunctiveBodyWitnessSurvives) {
  const Formula body{(a_ >= t_) || (a_ <= t_ - 2.0)};
  const Formula f{forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0), body))};
  PruneExpectSubsetAndWitness(f, MakeBox({{a_, Box::Interval(-3.0, 3.0)}}),
                              {{a_, 1.5}});
}

// Refutable instance: ∃a∈[1,2] ∀t∈[0,1]. a ≤ −t. At t=0 this needs a ≤ 0, which
// no a∈[1,2] meets — there is NO witness. The pre-pruner may shrink toward empty;
// whatever remains must still be ⊆ the input (it must never widen [1,2]).
TEST_F(ContractorIbexForallAdversarialTest, RefutableInstanceOnlyShrinks) {
  const Formula f{forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0), a_ <= -t_))};
  PruneExpectSubsetAndWitness(f, MakeBox({{a_, Box::Interval(1.0, 2.0)}}), {});
}

}  // namespace
}  // namespace dreal
