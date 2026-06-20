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
#include "dreal/contractor/contractor_fixpoint.h"

#include <vector>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/interval.h"
#include "dreal/util/rounding.h"

namespace dreal {
namespace {

using std::vector;

// Terminate the fixpoint when the box stops changing, with a hard pass cap so
// an asymptotically-shrinking (but never exactly-equal) sequence cannot loop
// forever. The empty cases short-circuit inside ContractorFixpoint::Prune
// before this is ever consulted; the cap only matters for the SAT control.
TerminationCondition MakeStableOrCappedTermCond(int* pass_counter) {
  return [pass_counter](const Box::IntervalVector& old_iv,
                        const Box::IntervalVector& new_iv,
                        const UpwardRounding&) {
    return (++(*pass_counter) >= 100) || (old_iv == new_iv);
  };
}

class ContractorFixpointTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};
};

// Phase-0 soundness net (bucket 3): an empty that arises *mid-composition* must
// halt the fixpoint and propagate to a final empty box. x*y == 1 is feasible on
// the wide initial box, but once x == 0 pins x to [0,0], re-applying x*y == 1 on
// the next pass empties at the root. This is the post-forward empty that is hard
// to force in a single Prune (HC4 incompleteness empties single constraints at
// the root) but reliably surfaces under iterated composition.
TEST_F(ContractorFixpointTest, CompositionEmptyPropagates) {
  const Formula f1{x_ * y_ == 1.0};
  const Formula f2{x_ == 0.0};
  box_[x_] = Box::Interval(-10.0, 10.0);
  box_[y_] = Box::Interval(-10.0, 10.0);
  ContractorStatus cs{box_};
  Config config{};
  const Contractor ctc1 = make_contractor_ibex_fwdbwd(f1, box_, config);
  const Contractor ctc2 = make_contractor_ibex_fwdbwd(f2, box_, config);
  int passes{0};
  const Contractor fp = make_contractor_fixpoint(
      MakeStableOrCappedTermCond(&passes), {ctc1, ctc2}, config);

  EXPECT_FALSE(cs.box().empty());
  { const UpwardRoundingScope rms_; fp.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "x*y == 1 with x pinned to 0 is infeasible; the fixpoint must empty "
         "the box.";
}

// Must-NOT-empty control: a coupled-but-consistent linear system. x+y == 1 and
// x-y == 0 has the unique solution x == y == 0.5 (exactly representable). The
// fixpoint contracts hard but must stay non-empty and keep the witness.
TEST_F(ContractorFixpointTest, CompositionContractsButStaysSat) {
  const Formula f1{x_ + y_ == 1.0};
  const Formula f2{x_ - y_ == 0.0};
  box_[x_] = Box::Interval(-10.0, 10.0);
  box_[y_] = Box::Interval(-10.0, 10.0);
  ContractorStatus cs{box_};
  Config config{};
  const Contractor ctc1 = make_contractor_ibex_fwdbwd(f1, box_, config);
  const Contractor ctc2 = make_contractor_ibex_fwdbwd(f2, box_, config);
  int passes{0};
  const Contractor fp = make_contractor_fixpoint(
      MakeStableOrCappedTermCond(&passes), {ctc1, ctc2}, config);

  { const UpwardRoundingScope rms_; fp.Prune(&cs, rms_.token()); }
  EXPECT_FALSE(cs.box().empty())
      << "x+y == 1 & x-y == 0 is satisfiable (x == y == 0.5); must not empty.";
  EXPECT_TRUE(cs.box()[x_].contains(0.5));
  EXPECT_TRUE(cs.box()[y_].contains(0.5));
}

}  // namespace
}  // namespace dreal
