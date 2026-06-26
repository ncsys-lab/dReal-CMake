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
#include "dreal/contractor/contractor_ibex_acid.h"

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::vector;

class ContractorIbexAcidTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};
  Config acid_config_;
  ContractorIbexAcidTest() {
    acid_config_.mutable_use_acid().set_from_command_line(true);
  }
};

// Soundness: on a SAT constraint, ACID keeps the box non-empty, keeps the
// witness inside, and actually contracts (ub falls from 10 to ~2). The last
// assertion fails if ACID were a no-op/dummy, so it doubles as a wiring check.
// x*x == 4 with x in [0,10] is satisfiable at x == 2.
TEST_F(ContractorIbexAcidTest, SatStaysNonEmptyAndContracts) {
  const Formula f{x_ * x_ == 4.0};
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexAcid ctc{{f}, box_, acid_config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_FALSE(cs.box().empty());
  EXPECT_TRUE(cs.box()[x_].contains(2.0));
  EXPECT_LT(cs.box()[x_].ub(), 9.0)
      << "ACID must actually contract the box (a no-op would leave ub at 10).";
}

// Refutation: on an infeasible constraint, ACID empties the box (tanh's range
// is (-1,1), so tanh(x) == 2 is unsatisfiable everywhere).
TEST_F(ContractorIbexAcidTest, UnsatEmpties) {
  const Formula f{tanh(x_) == 2.0};
  box_[x_] = Box::Interval(-5.0, 5.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexAcid ctc{{f}, box_, acid_config_};

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_TRUE(cs.box().empty());
}

// ACID applies HC4 and then shaves, so its result is always a subset of a
// single HC4 forward-backward pass. This proves ACID is at least as strong as
// HC4 (never weaker -> no soundness loss) and that it is genuinely wired.
TEST_F(ContractorIbexAcidTest, AtLeastAsStrongAsHc4) {
  const Formula f{x_ * x_ == 4.0};
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 1.0);

  ContractorStatus cs_hc4{box_};
  const ContractorIbexFwdbwd hc4{f, box_, Config{}};
  { const UpwardRoundingScope rms_; hc4.Prune(&cs_hc4, rms_.token()); }

  ContractorStatus cs_acid{box_};
  const ContractorIbexAcid acid{{f}, box_, acid_config_};
  { const UpwardRoundingScope rms_; acid.Prune(&cs_acid, rms_.token()); }

  ASSERT_FALSE(cs_hc4.box().empty());
  ASSERT_FALSE(cs_acid.box().empty());
  EXPECT_TRUE(cs_acid.box()[x_].is_subset(cs_hc4.box()[x_]))
      << "ACID applies HC4 then shaves; its box must be a subset of HC4's.";
}

// The 3BCID variant satisfies the same soundness contract via --3bcid.
TEST_F(ContractorIbexAcidTest, ThreeBCidSatStaysNonEmptyAndContracts) {
  Config cfg;
  cfg.mutable_use_3bcid().set_from_command_line(true);
  const Formula f{x_ * x_ == 4.0};
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexAcid ctc{{f}, box_, cfg};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_FALSE(cs.box().empty());
  EXPECT_TRUE(cs.box()[x_].contains(2.0));
  EXPECT_LT(cs.box()[x_].ub(), 9.0);
}

}  // namespace
}  // namespace dreal
