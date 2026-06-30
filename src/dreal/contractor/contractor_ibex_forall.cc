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

#include <cmath>
#include <utility>
#include <vector>

#include "dreal/solver/filter_assertion.h"
#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/nnfizer.h"

namespace dreal {

using std::make_unique;
using std::ostream;
using std::vector;

namespace {
// The joint ibex variable ordering: existential (box) variables first, then the
// universal (∀-bound) variables. Existential indices [0, n_exist) are the
// CtcForAll "variables" to contract; the rest are its "parameters" y.
vector<Variable> JointVars(const Box& box, const Formula& f) {
  vector<Variable> joint{box.variables()};
  for (const Variable& v : get_quantified_variables(f)) {
    joint.push_back(v);
  }
  return joint;
}
}  // namespace

ContractorIbexForall::ContractorIbexForall(Formula f, const Box& box,
                                           const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_FORALL, DynamicBitset(box.size()),
                     config},
      f_{std::move(f)},
      ibex_converter_{JointVars(box, f_)} {
  DREAL_ASSERT(is_forall(f_));
  DREAL_LOG_DEBUG("ContractorIbexForall::ContractorIbexForall");

  // 1. Collect the relational-atom leaves of the NNF body into one System (NNF
  // pushes negations onto atoms so the tree is only ∧/∨/atom). Sharing one
  // variable space is mandatory: IBEX forbids a symbol belonging to two
  // Functions, so per-leaf NumConstraints would throw.
  const Formula body{Nnfizer{}.Convert(get_quantified_formula(f_), true)};
  system_factory_ = std::make_unique<ibex::SystemFactory>();
  system_factory_->add_var(ibex_converter_.variables());
  const bool collected{CollectLeaves(body)};
  ibex_converter_.set_need_to_delete_variables(true);
  if (!collected) {
    // A non-relational leaf (e.g. a residual Boolean) — not buildable. No-op;
    // CEGIS still decides.
    is_dummy_ = true;
    return;
  }
  system_ = std::make_unique<ibex::System>(*system_factory_);
  if (system_->nb_ctr == 0) {
    is_dummy_ = true;
    return;
  }
  int next_leaf{0};
  ibex::Ctc* const inner{Materialize(body, &next_leaf)};

  // 2. y_init: the universal-variable box. FilterAssertion absorbs the simple
  // bound atoms of ¬body = (binder domain ∧ ¬inner); the binder bounds are
  // always top-level conjuncts, so the resulting box is ⊆ the binder domain —
  // every sampled y is a real universal point, which is what makes the proj-
  // intersection sound (it can only contract against true ∀-obligations).
  vector<Variable> universal_vec;
  for (const Variable& v : get_quantified_variables(f_)) {
    universal_vec.push_back(v);
  }
  Box ubox{universal_vec};
  const Formula neg_body{Nnfizer{}.Convert(!get_quantified_formula(f_), true)};
  if (is_conjunction(neg_body)) {
    for (const Formula& g : get_operands(neg_body)) {
      if (is_relational(g)) {
        FilterAssertion(g, &ubox);
      }
    }
  } else if (is_relational(neg_body)) {
    FilterAssertion(neg_body, &ubox);
  }
  // CtcForAll bisects y_init; an unbounded universal dimension is not bisectable.
  for (int i = 0; i < ubox.size(); ++i) {
    if (!std::isfinite(ubox[i].lb()) || !std::isfinite(ubox[i].ub())) {
      is_dummy_ = true;
      return;
    }
  }
  const ibex::IntervalVector y_init{ubox.interval_vector()};

  // 3. The variable/parameter split: existential indices [0, box.size()) are
  // CtcForAll "variables".
  const int joint_dim{static_cast<int>(JointVars(box, f_).size())};
  variable_bits_ = make_unique<ibex::BitSet>(joint_dim);
  for (int i = 0; i < box.size(); ++i) {
    variable_bits_->add(i);
  }
  forall_ctc_ = make_unique<ibex::CtcForAll>(*inner, *variable_bits_, y_init,
                                             config.forall_pre_prune_prec());

  // 4. Input bitset: the existential variables free in f_.
  DynamicBitset& input{mutable_input()};
  for (const Variable& var : f_.GetFreeVariables()) {
    input.set(box.index(var));
  }
  set_include_forall();
}

bool ContractorIbexForall::CollectLeaves(const Formula& f) {
  if (is_conjunction(f) || is_disjunction(f)) {
    for (const Formula& g : get_operands(f)) {
      if (!CollectLeaves(g)) {
        return false;
      }
    }
    return true;
  }
  if (!is_relational(f)) {
    return false;  // True/Variable/… — not a buildable leaf.
  }
  const ibex::ExprCtr* const expr_ctr{ibex_converter_.Convert(f)};
  if (expr_ctr == nullptr) {
    return false;
  }
  system_factory_->add_ctr(*expr_ctr);
  expr_ctrs_.emplace_back(expr_ctr);
  return true;
}

ibex::Ctc* ContractorIbexForall::Materialize(const Formula& f, int* next_leaf) {
  if (is_conjunction(f) || is_disjunction(f)) {
    vector<ibex::Ctc*> kids;
    kids.reserve(get_operands(f).size());
    for (const Formula& g : get_operands(f)) {
      kids.push_back(Materialize(g, next_leaf));
    }
    if (is_conjunction(f)) {
      owned_ctcs_.push_back(
          make_unique<ibex::CtcCompo>(ibex::Array<ibex::Ctc>(kids)));
    } else {
      owned_ctcs_.push_back(
          make_unique<ibex::CtcUnion>(ibex::Array<ibex::Ctc>(kids)));
    }
    return owned_ctcs_.back().get();
  }
  // Leaf: the (*next_leaf)-th System constraint, added in this same DFS order by
  // CollectLeaves — so the index is consistent across the two passes.
  owned_ctcs_.push_back(make_unique<ibex::CtcFwdBwd>(*system_, (*next_leaf)++));
  return owned_ctcs_.back().get();
}

void ContractorIbexForall::Prune(ContractorStatus* cs,
                                 const UpwardRounding& ur) const {
  DREAL_ASSERT(!is_dummy_ && forall_ctc_);

  // CtcForAll's inner CtcFwdBwd runs gaol interval arithmetic, sound only under
  // FE_UPWARD — established once per ICP phase by the caller's
  // UpwardRoundingScope and witnessed by `ur` (same contract as
  // ContractorIbexPolytope; no per-call fesetround).
  (void)ur;
  DREAL_ASSERT_ROUNDING(FE_UPWARD);

  // cs's box holds exactly the existential variables (size == CtcForAll's
  // nb_var); CtcForAll assembles the joint box internally with y_init.
  Box::IntervalVector& iv{cs->mutable_box().mutable_interval_vector()};

  thread_local std::vector<std::pair<int, ibex::Interval>> saved;
  saved.clear();
  {
    DynamicBitset::size_type i{input().find_first()};
    while (i != DynamicBitset::npos) {
      saved.emplace_back(static_cast<int>(i), iv[i]);
      i = input().find_next(i);
    }
  }

  forall_ctc_->contract(iv);

  bool changed{false};
  if (iv.is_empty()) {
    cs->mutable_output().set();
    changed = true;
  } else {
    for (const auto& [idx, old] : saved) {
      if (iv[idx] != old) {
        cs->mutable_output().set(static_cast<DynamicBitset::size_type>(idx));
        changed = true;
      }
    }
  }
  if (changed) {
    cs->AddUsedConstraint(f_);
  }
}

ostream& ContractorIbexForall::display(ostream& os) const {
  return os << "IbexForall(" << f_ << ")";
}

bool ContractorIbexForall::is_dummy() const { return is_dummy_; }

}  // namespace dreal
