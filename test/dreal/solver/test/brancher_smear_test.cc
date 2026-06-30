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

#include "dreal/solver/config.h"  // SmearVariant
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
  const Variable z_{"z", Variable::Type::CONTINUOUS};

  // Runs `variant` on `box`/`fes` and returns the chosen dimension.
  static int Choose(const vector<FormulaEvaluator>& fes, const Box& box,
                    SmearVariant variant) {
    const SmearBrancher smear{fes, box, variant};
    DynamicBitset active(box.size());
    for (int i = 0; i < static_cast<int>(box.size()); ++i) active.set(i);
    Box left{box};
    Box right{box};
    const UpwardRoundingScope rms;
    return smear(box, active, &left, &right, rms.token());
  }
};

// smearsumrel must pick the Jacobian-dominant variable, NOT merely the widest.
// For x + 100*y == 0 the Jacobian is [1, 100]; with widths w_x=20, w_y=2 the
// single-constraint score is |J_j|*w_j/NC: x = 1*20/220 = 0.09, y = 100*2/220 =
// 0.91 -> y wins, even though x is 10x wider. Largest-first would pick x, so
// this assertion distinguishes smear from the default brancher.
TEST_F(SmearBrancherTest, PicksJacobianDominantVariableNotWidest) {
  Box box{{x_, y_}};
  box[x_] = Box::Interval(-10, 10);  // width 20
  box[y_] = Box::Interval(-1, 1);    // width 2
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(x_ + 100 * y_ == 0)};

  Box left{box};
  Box right{box};
  int dim{-2};
  {
    const SmearBrancher smear{fes, box, SmearVariant::kSumRel};
    DynamicBitset active(box.size());
    active.set(box.index(x_));
    active.set(box.index(y_));
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
  Box box{{x_, y_}};
  box[x_] = Box::Interval(-10, 10);
  box[y_] = Box::Interval(-1, 1);
  const vector<FormulaEvaluator> fes{};
  EXPECT_EQ(Choose(fes, box, SmearVariant::kSumRel), box.index(x_))
      << "no constraints -> fallback largest-first picks the widest dim (x).";
}

// SmearMax (max impact over constraints) and the relative variants diverge on
//   c1: 100x + 60y + z == 0   ->  J row [100, 60, 1]
//   c2:   1x + 60y + z == 0   ->  J row [  1, 60, 1]
// with unit widths (each var in [0,1]). Per-variable scores:
//   sum:  x=101, y=120, z=2                       -> y
//   max:  x=max(100,1)=100, y=60, z=1             -> x  (the lone 100-spike)
//   NC1=161, NC2=62
//   sumrel: x=100/161+1/62=0.637, y=0.373+0.968=1.341, z=0.022 -> y
// So kMax picks x while kSum/kSumRel pick y: the variant switch changes the
// pick, proving the parameterization is live (not a relabel of smearsumrel).
TEST_F(SmearBrancherTest, MaxSpikeVsSummedImpact) {
  Box box{{x_, y_, z_}};
  box[x_] = Box::Interval(0, 1);
  box[y_] = Box::Interval(0, 1);
  box[z_] = Box::Interval(0, 1);
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(100 * x_ + 60 * y_ + z_ == 0),
      make_relational_formula_evaluator(1 * x_ + 60 * y_ + z_ == 0)};

  EXPECT_EQ(Choose(fes, box, SmearVariant::kMax), box.index(x_))
      << "SmearMax follows the lone 100-magnitude spike on x.";
  EXPECT_EQ(Choose(fes, box, SmearVariant::kSum), box.index(y_))
      << "SmearSum follows the larger column sum on y.";
  EXPECT_EQ(Choose(fes, box, SmearVariant::kSumRel), box.index(y_))
      << "SmearSumRelative also picks y here.";
}

// SmearSum (absolute) vs SmearSumRelative (normalized): a high-magnitude
// constraint dominates the absolute sum but is normalized away in the relative
// one.
//   c1: 10x + 8y == 0   ->  J row [10, 8, 0]   NC1 = 18
//   c2:  1y + 5z == 0   ->  J row [ 0, 1, 5]   NC2 = 6
// unit widths. Scores:
//   sum:    x=10, y=8+1=9, z=5                          -> x  (raw 10-spike)
//   sumrel: x=10/18=0.556, y=8/18+1/6=0.611, z=5/6=0.833 -> z  (c2 reweighted up)
TEST_F(SmearBrancherTest, AbsoluteSumVsNormalizedSum) {
  Box box{{x_, y_, z_}};
  box[x_] = Box::Interval(0, 1);
  box[y_] = Box::Interval(0, 1);
  box[z_] = Box::Interval(0, 1);
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(10 * x_ + 8 * y_ == 0),
      make_relational_formula_evaluator(1 * y_ + 5 * z_ == 0)};

  EXPECT_EQ(Choose(fes, box, SmearVariant::kSum), box.index(x_))
      << "SmearSum follows the raw 10-magnitude entry on x.";
  EXPECT_EQ(Choose(fes, box, SmearVariant::kSumRel), box.index(z_))
      << "SmearSumRelative normalizes the big constraint away and picks z.";
}

// SmearMaxRelative (max normalized impact) vs SmearSumRelative (summed
// normalized impact):
//   c1: 9x + 1y == 0   ->  J row [9, 1, 0]   NC1 = 10
//   c2: 5y + 1z == 0   ->  J row [0, 5, 1]   NC2 = 6
// unit widths. Normalized shares:
//   x: c1 0.9               -> sumrel 0.9,           maxrel 0.9
//   y: c1 0.1, c2 5/6=0.833 -> sumrel 0.933,         maxrel 0.833
//   z: c2 1/6=0.167         -> sumrel 0.167,         maxrel 0.167
// sumrel picks y (0.933 summed), maxrel picks x (its single 0.9 share beats
// y's per-constraint max of 0.833).
TEST_F(SmearBrancherTest, MaxRelativeVsSumRelative) {
  Box box{{x_, y_, z_}};
  box[x_] = Box::Interval(0, 1);
  box[y_] = Box::Interval(0, 1);
  box[z_] = Box::Interval(0, 1);
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(9 * x_ + 1 * y_ == 0),
      make_relational_formula_evaluator(5 * y_ + 1 * z_ == 0)};

  EXPECT_EQ(Choose(fes, box, SmearVariant::kSumRel), box.index(y_))
      << "SmearSumRelative sums y's two normalized shares (0.933) to win.";
  EXPECT_EQ(Choose(fes, box, SmearVariant::kMaxRel), box.index(x_))
      << "SmearMaxRelative ranks by single best share, so x's 0.9 wins.";
}

}  // namespace
}  // namespace dreal
