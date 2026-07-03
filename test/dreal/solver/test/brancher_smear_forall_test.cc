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
#include "dreal/solver/brancher_smear.h"

#include <vector>

#include <gtest/gtest.h>

#include "dreal/solver/formula_evaluator.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/dynamic_bitset.h"
#include "dreal/util/rounding.h"

namespace dreal {
namespace {

using std::vector;

// When the only constraint is a `forall`, the smear brancher currently skips it
// (brancher_smear.cc: `if (is_forall(f) ...) continue;`), leaving zero Jacobian
// rows -> is_dummy_ -> largest-first fallback. This test pins the desired
// behavior: the forall BODY's sensitivity to the existential (box) variables
// drives the split-variable choice.
//
// Setup makes the two heuristics disagree, so the test discriminates:
//   box: a in [-1, 1] (width 2, the WIDEST)   -> largest-first picks a
//        b in [-0.5, 0.5] (width 1)
//   body: forall t in [0,1]. a + 100*b + t <= 5
//        d/da = 1, d/db = 100  (t is universal, never a split candidate)
//        smearsum score: |1|*2 = 2 for a, |100|*1 = 100 for b -> smear picks b
//
// BEFORE the forall-body-Jacobian change: is_dummy_ -> largest-first -> a.
// AFTER: smear scores b highest -> b. So this asserts the brancher returns b.
class BrancherSmearForallTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};  // existential (widest)
  const Variable b_{"b", Variable::Type::CONTINUOUS};  // existential (sensitive)
  const Variable t_{"t", Variable::Type::CONTINUOUS};  // universal
};

TEST_F(BrancherSmearForallTest, ForallBodyDrivesExistentialSplitChoice) {
  Box box{{a_, b_}};
  box[a_] = Box::Interval(-1.0, 1.0);   // width 2 (widest)
  box[b_] = Box::Interval(-0.5, 0.5);   // width 1

  // The quantified matrix must be a flat clause (forall_formula_evaluator.cc
  // asserts is_clause) — real benchmarks reach that shape via the Tseitin
  // CNFizer; a unit test supplies it directly. `t<0 || t>1` encodes the binder
  // domain t∈[0,1] (its negation, recovered for the Jacobian, is t≥0 ∧ t≤1).
  const Formula f{forall(
      {t_}, (t_ < 0.0) || (t_ > 1.0) || (a_ + 100.0 * b_ + t_ <= 5.0))};

  vector<FormulaEvaluator> fes;
  fes.push_back(make_forall_formula_evaluator(f, 0.001, 0.001, 1));

  const SmearBrancher sb{fes, box, SmearVariant::kSum};

  DynamicBitset active{static_cast<DynamicBitset::size_type>(box.size())};
  active.set(box.index(a_));
  active.set(box.index(b_));

  Box left;
  Box right;
  const UpwardRoundingScope rms;
  const int dim{sb(box, active, &left, &right, rms.token())};

  EXPECT_EQ(dim, box.index(b_))
      << "smear should pick b (score 100) over the wider a (score 2): the "
         "forall body's Jacobian must drive the existential split choice, not "
         "largest-first.";
}

}  // namespace
}  // namespace dreal
