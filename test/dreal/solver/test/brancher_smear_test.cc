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

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/solver/formula_evaluator.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::vector;

class SmearBrancherTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
};

// smearsumrel must pick the Jacobian-dominant variable, NOT merely the widest.
// For x + 100*y == 0 the Jacobian is [1, 100]; with widths w_x=20, w_y=2 the
// single-constraint score is |J_j|*w_j/NC: x = 1*20/220 = 0.09, y = 100*2/220 =
// 0.91 -> y wins, even though x is 10x wider. Largest-first would pick x, so
// this assertion distinguishes smear from the default brancher.
TEST_F(SmearBrancherTest, PicksJacobianDominantVariableNotWidest) {
  Box box{vars_};
  box[x_] = Box::Interval(-10, 10);  // width 20
  box[y_] = Box::Interval(-1, 1);    // width 2
  vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(x_ + 100 * y_ == 0)};
  const SmearBrancher smear{fes, box, 0.5};

  DynamicBitset active(box.size());
  active.set(box.index(x_));
  active.set(box.index(y_));
  Box left{vars_};
  Box right{vars_};
  int dim{-2};
  {
    const UpwardRoundingScope rms;
    dim = smear(box, active, &left, &right, rms.token());
  }

  EXPECT_EQ(dim, box.index(y_))
      << "smearsumrel should pick y (Jacobian 100 dominates), not the wider x.";
  // The chosen dimension is partitioned; the others are untouched.
  EXPECT_EQ(left[x_], box[x_]);
  EXPECT_EQ(right[x_], box[x_]);
  EXPECT_EQ(left[y_].lb(), box[y_].lb());
  EXPECT_EQ(right[y_].ub(), box[y_].ub());
  EXPECT_EQ(left[y_].ub(), right[y_].lb());  // contiguous split (midpoint)
}

// With no relational constraints the System is empty, so smear falls back to
// largest-first, which picks the widest dimension (x).
TEST_F(SmearBrancherTest, FallsBackToLargestFirstWhenNoConstraints) {
  Box box{vars_};
  box[x_] = Box::Interval(-10, 10);
  box[y_] = Box::Interval(-1, 1);
  const vector<FormulaEvaluator> fes{};
  const SmearBrancher smear{fes, box, 0.5};

  DynamicBitset active(box.size());
  active.set(box.index(x_));
  active.set(box.index(y_));
  Box left{vars_};
  Box right{vars_};
  int dim{-2};
  {
    const UpwardRoundingScope rms;
    dim = smear(box, active, &left, &right, rms.token());
  }
  EXPECT_EQ(dim, box.index(x_))
      << "no constraints -> fallback largest-first picks the widest dim (x).";
}

}  // namespace
}  // namespace dreal
