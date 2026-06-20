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
#include "dreal/contractor/contractor_ibex_fwdbwd.h"

#include <sstream>
#include <utility>
#include <vector>

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/math.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/rounding.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"

using std::cout;
using std::make_unique;
using std::ostream;
using std::ostringstream;

namespace dreal {

namespace {
class ContractorIbexFwdbwdStat : public Stat {
 public:
  explicit ContractorIbexFwdbwdStat(const bool enabled) : Stat{enabled} {};
  ContractorIbexFwdbwdStat(const ContractorIbexFwdbwdStat&) = delete;
  ContractorIbexFwdbwdStat(ContractorIbexFwdbwdStat&&) = delete;
  ContractorIbexFwdbwdStat& operator=(const ContractorIbexFwdbwdStat&) = delete;
  ContractorIbexFwdbwdStat& operator=(ContractorIbexFwdbwdStat&&) = delete;
  ~ContractorIbexFwdbwdStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-fwdbwd Pruning", "Pruning level", num_pruning_);
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-fwdbwd Pruning (zero-effect)", "Pruning level",
            num_zero_effect_pruning_);
      if (num_pruning_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Pruning", "Pruning level",
              timer_pruning_.seconds());
      }
    }
  }

  int num_zero_effect_pruning_{0};
  int num_pruning_{0};

  Timer timer_pruning_;
};
}  // namespace

//---------------------------------------
// Implementation of ContractorIbexFwdbwd
//---------------------------------------
ContractorIbexFwdbwd::ContractorIbexFwdbwd(Formula f, const Box& box,
                                           const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_FWDBWD, DynamicBitset(box.size()),
                     config},
      f_{std::move(f)},
      ibex_converter_{box} {
  // Build num_ctr and ctc_.
  expr_ctr_.reset(ibex_converter_.Convert(f_));
  if (expr_ctr_) {
    num_ctr_ = make_unique<ibex::NumConstraint>(ibex_converter_.variables(),
                                                *expr_ctr_);
    // Build input.
    DynamicBitset& input{mutable_input()};
    for (const Variable& var : f_.GetFreeVariables()) {
      input.set(box.index(var));
    }
  } else {
    is_dummy_ = true;
  }
}

void ContractorIbexFwdbwd::Prune(ContractorStatus* cs, const UpwardRounding& ur) const {
  thread_local ContractorIbexFwdbwdStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_ASSERT(!is_dummy_ && num_ctr_);

  Box::IntervalVector& iv{cs->mutable_box().mutable_interval_vector()};
  DREAL_LOG_TRACE("ContractorIbexFwdbwd::Prune");
  DREAL_LOG_TRACE("CTC = {}", fmt::streamed(*num_ctr_));
  DREAL_LOG_TRACE("F = {}", f_);
  stat.timer_pruning_.resume();

  // gaol (ibex's interval backend) is only sound with the FPU in round-upward
  // mode; under any other mode its directed rounding inverts (lo>hi) and an
  // inexact constant subexpression collapses to an empty interval (false
  // UNSAT). FE_UPWARD is established once per ICP phase by the caller's
  // UpwardRoundingScope and proven here by the `ur` token — so this hot Prune
  // no longer pays a per-call fesetround. The assert verifies the inherited
  // phase mode in Debug. See gaol_directed_rounding_false_unsat_test.cc.
  DREAL_ASSERT_ROUNDING(FE_UPWARD);

  // Track which variables narrowed via the ibex fork's backward-callback
  // (commit 4d61b841 of the dreal-perf-patches branch). The callback fires
  // once per narrowed variable with (var_idx, before, after), which lets us
  // populate the output bitset directly without a before/after snapshot.
  // For boxes with 50-500 variables and a fwdbwd constraint touching only
  // 2-10, this beats the snapshot pattern by both allocation count and
  // comparison cost.
  bool changed{false};
  // Token-gated wrapper for ibex's HC4 backward (see util/rounded_interval.h) — the
  // `ur` proves FE_UPWARD is established, and routing through the wrapper lets
  // the rounding lint forbid any raw ibex::Function::backward call.
  const bool is_inner{
    ibex_hc4_backward(
      num_ctr_->f, num_ctr_->right_hand_side(), iv,
      [cs, &changed](int var_idx,
                     const ibex::Interval& /*before*/,
                     const ibex::Interval& /*after*/) {
        cs->mutable_output().set(static_cast<DynamicBitset::size_type>(var_idx));
        changed = true;
      },
      ur)
  }; // true if iv was already inner (unchanged).
  stat.timer_pruning_.pause();
  if (stat.enabled()) {
    stat.num_pruning_++;
  }
  // iv.is_empty() can be true without any per-variable callback firing if
  // the constraint is infeasible globally — set every output bit in that case.
  if (!is_inner && iv.is_empty()) {
    cs->mutable_output().set();
    changed = true;
  }
  // Update used constraints.
  if (changed) {
    cs->AddUsedConstraint(f_);
    if (stat.enabled()) {
      ostringstream oss;
      // DisplayDiff(oss, cs->box().variables(), old_iv,
      // cs->box().interval_vector());
      DREAL_LOG_TRACE("Changed\n{}", oss.str());
    }
  } else {
    if (stat.enabled()) {
      stat.num_zero_effect_pruning_++;
    }
    DREAL_LOG_TRACE("NO CHANGE");
  }
}

ostream& ContractorIbexFwdbwd::display(ostream& os) const {
  DREAL_ASSERT(num_ctr_);
  return os << "IbexFwdbwd(" << *num_ctr_ << ")";
}

bool ContractorIbexFwdbwd::is_dummy() const { return is_dummy_; }

}  // namespace dreal
