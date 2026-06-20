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
#include "dreal/contractor/contractor_seq.h"

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

class ContractorSeqTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};
};

// Phase-0 soundness net (bucket 3): an empty produced by the second contractor
// of a sequential composition must leave the final box empty. The first
// contractor pins x to [0,0]; the second (x*y == 1) is then infeasible and
// empties. Guards that the empty signal survives sequential composition.
TEST_F(ContractorSeqTest, SequentialEmptyPropagates) {
  const Formula f1{x_ == 0.0};
  const Formula f2{x_ * y_ == 1.0};
  box_[x_] = Box::Interval(-10.0, 10.0);
  box_[y_] = Box::Interval(-10.0, 10.0);
  ContractorStatus cs{box_};
  Config config{};
  const Contractor ctc1 = make_contractor_ibex_fwdbwd(f1, box_, config);
  const Contractor ctc2 = make_contractor_ibex_fwdbwd(f2, box_, config);
  const Contractor seq = make_contractor_seq({ctc1, ctc2}, config);

  EXPECT_FALSE(cs.box().empty());
  { const UpwardRoundingScope rms_; seq.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "After x is pinned to 0, x*y == 1 is infeasible; the sequence must "
         "empty the box.";
}

}  // namespace
}  // namespace dreal
