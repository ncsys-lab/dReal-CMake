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

#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <dreal/util/rounded_interval.h>

#include "dreal/solver/brancher.h"          // BranchLargestFirst
#include "dreal/solver/filter_assertion.h"  // FilterAssertion (∀ domain recovery)
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/assert.h"
#include "dreal/util/nnfizer.h"

namespace dreal {

using std::make_unique;
using std::unique_ptr;
using std::vector;

namespace {
// Recover a forall's universal (∀-bound) binder box by absorbing the simple
// bounds of the NNF of ¬body = (binder domain ∧ ¬inner); the binder bounds are
// always top-level conjuncts there. Mirrors ContractorIbexForall's y_init.
// Returns nullopt if any universal dimension is unbounded — its Jacobian column
// would evaluate to an infinite interval, so the forall contributes no rows.
std::optional<Box> RecoverUniversalBox(const Formula& f) {
  vector<Variable> universal_vec;
  for (const Variable& v : get_quantified_variables(f)) {
    universal_vec.push_back(v);
  }
  if (universal_vec.empty()) return std::nullopt;
  Box ubox{universal_vec};
  const Formula neg_body{Nnfizer{}.Convert(!get_quantified_formula(f), true)};
  if (is_conjunction(neg_body)) {
    for (const Formula& g : get_operands(neg_body)) {
      if (is_relational(g)) FilterAssertion(g, &ubox);
    }
  } else if (is_relational(neg_body)) {
    FilterAssertion(neg_body, &ubox);
  }
  for (int i = 0; i < ubox.size(); ++i) {
    if (!std::isfinite(ubox[i].lb()) || !std::isfinite(ubox[i].ub())) {
      return std::nullopt;
    }
  }
  return ubox;
}

// The joint ibex variable ordering: box (existential) variables, then the
// finite-domain universal variables of each plain (non-ODE) forall. Universal
// vars are DEDUPLICATED by id: CNF splits one source forall into several
// clauses that share the same ∀-bound variable, so appending per-forall would
// register duplicate ibex symbols (an ibex-fatal aliasing -> SIGBUS).
vector<Variable> ComputeJointVars(const vector<FormulaEvaluator>& fes,
                                  const Box& box) {
  vector<Variable> joint{box.variables()};
  std::unordered_set<Variable::Id> seen;
  for (const Variable& v : joint) seen.insert(v.get_id());
  for (const FormulaEvaluator& fe : fes) {
    const Formula& f = fe.formula();
    if (!is_forall(f) || f.include_ode()) continue;
    if (RecoverUniversalBox(f)) {
      for (const Variable& v : get_quantified_variables(f)) {
        if (seen.insert(v.get_id()).second) joint.push_back(v);
      }
    }
  }
  return joint;
}
}  // namespace

SmearBrancher::SmearBrancher(
    const vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    const SmearVariant variant)
    : variant_{variant},
      joint_vars_{ComputeJointVars(formula_evaluators, box)},
      ibex_converter_{joint_vars_},
      // Paren-init: ibex::IntervalVector(int n) sizes the vector; brace-init
      // would hit its initializer_list ctor and make a size-1 vector of value n.
      universal_iv_(static_cast<int>(joint_vars_.size())) {
  DREAL_ASSERT(variant_ != SmearVariant::kNone);
  // Assemble one ibex::System over the joint variables: plain NRA constraints
  // (box vars only) plus the body leaves of each finite-domain forall (box +
  // universal vars). ODE constraints are skipped (IbexConverter throws on them).
  // Built from joint_vars_, so column k == box dimension k for k < box.size().
  system_factory_ = make_unique<ibex::SystemFactory>();
  system_factory_->add_var(ibex_converter_.variables());
  // Map each joint variable's id to its column (universal vars keyed here so a
  // forall's binder intervals land in the right column regardless of order).
  std::unordered_map<Variable::Id, int> col_of;
  for (int i = 0; i < static_cast<int>(joint_vars_.size()); ++i) {
    col_of.emplace(joint_vars_[i].get_id(), i);
  }
  int n_ctr = 0;
  for (const FormulaEvaluator& fe : formula_evaluators) {
    const Formula& f = fe.formula();
    if (f.include_ode()) continue;
    if (is_forall(f)) {
      const std::optional<Box> ubox{RecoverUniversalBox(f)};
      if (!ubox) continue;  // unbounded ∀ domain -> no rows (matches skip above)
      // Pin this forall's universal vars at their binder intervals (shared vars
      // across CNF-split clauses map to the same column — idempotent).
      for (int i = 0; i < ubox->size(); ++i) {
        universal_iv_[col_of.at(ubox->variable(i).get_id())] = (*ubox)[i];
      }
      n_ctr += AddForallLeaves(Nnfizer{}.Convert(get_quantified_formula(f), true),
                               box);
      continue;
    }
    unique_ptr<const ibex::ExprCtr, ExprCtrDeleter> expr_ctr{
        ibex_converter_.Convert(f)};
    if (expr_ctr) {
      system_factory_->add_ctr(*expr_ctr);
      expr_ctrs_.push_back(std::move(expr_ctr));
      ++n_ctr;
    }
  }
  ibex_converter_.set_need_to_delete_variables(true);
  if (n_ctr == 0) {
    is_dummy_ = true;
    return;
  }
  system_ = make_unique<ibex::System>(*system_factory_);
  if (system_->nb_ctr == 0) is_dummy_ = true;
}

int SmearBrancher::AddForallLeaves(const Formula& f, const Box& box) {
  if (is_conjunction(f) || is_disjunction(f)) {
    int added = 0;
    for (const Formula& g : get_operands(f)) added += AddForallLeaves(g, box);
    return added;
  }
  if (!is_relational(f)) return 0;  // residual Boolean/True — not a leaf.
  // Universal-only atoms (binder domain, antecedent) have zero existential
  // columns; drop them to keep the System small.
  bool touches_box = false;
  for (const Variable& v : f.GetFreeVariables()) {
    if (box.has_variable(v)) {
      touches_box = true;
      break;
    }
  }
  if (!touches_box) return 0;
  unique_ptr<const ibex::ExprCtr, ExprCtrDeleter> expr_ctr{
      ibex_converter_.Convert(f)};
  if (!expr_ctr) return 0;
  system_factory_->add_ctr(*expr_ctr);
  expr_ctrs_.push_back(std::move(expr_ctr));
  return 1;
}

int SmearBrancher::operator()(const Box& box, const DynamicBitset& active_set,
                              Box* const left, Box* const right,
                              const UpwardRounding& ur) const {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  DREAL_ASSERT(!active_set.none());
  if (is_dummy_) {
    return BranchLargestFirst(box, active_set, left, right, ur);
  }

  const Box::IntervalVector& iv{box.interval_vector()};
  const int n{static_cast<int>(box.size())};
  // Joint eval vector: box (existential) intervals in [0,n), universal binder
  // intervals (from universal_iv_) in [n, joint_dim). Interval Jacobian is
  // nb_ctr x joint_dim; only columns k < n (box dims) are scored below. gaol
  // AD -> needs FE_UPWARD (ur). With no forall, joint_dim == n and jv == iv.
  ibex::IntervalVector jv{universal_iv_};
  for (int k = 0; k < n; ++k) jv[k] = iv[k];
  const ibex::IntervalMatrix J{system_->f_ctrs.jacobian(jv)};

  vector<double> w(n, 0.0);
  for (int k = 0; k < n; ++k) w[k] = safe_diam(iv[k], ur);

  // IBEX's four SmearFunction variants on two binary axes: the per-constraint
  // impact is |J[i][k]|·w[k] (absolute) or divided by NC_i = Σ_k |J[i][k]|·w[k]
  // (relative), and the per-variable score aggregates those impacts over
  // constraints by sum or by max. Scores are >= 0, so a max aggregation starts
  // from 0.
  const bool relative{variant_ == SmearVariant::kSumRel ||
                      variant_ == SmearVariant::kMaxRel};
  const bool use_max{variant_ == SmearVariant::kMax ||
                     variant_ == SmearVariant::kMaxRel};
  vector<double> score(n, 0.0);
  for (int i = 0; i < system_->nb_ctr; ++i) {
    double nc = 1.0;
    if (relative) {
      nc = 0.0;
      for (int k = 0; k < n; ++k) nc += J[i][k].mag() * w[k];
      if (!(nc > 0.0) || !std::isfinite(nc)) continue;  // uninformative row
    }
    for (int k = 0; k < n; ++k) {
      const double impact{J[i][k].mag() * w[k] / nc};
      if (!std::isfinite(impact)) continue;  // infinite derivative: skip
      if (use_max) {
        if (impact > score[k]) score[k] = impact;
      } else {
        score[k] += impact;
      }
    }
  }

  int best{-1};
  double best_score{-1.0};
  DynamicBitset::size_type j{active_set.find_first()};
  while (j != DynamicBitset::npos) {
    if (box[j].is_bisectable() && score[j] > best_score) {
      best_score = score[j];
      best = static_cast<int>(j);
    }
    j = active_set.find_next(j);
  }
  // Uninformative Jacobian (all scores 0 / non-finite) -> largest-first, as in
  // IBEX's SmearFunction. This is the algorithm's defined behavior, not an
  // error path: every variable choice is sound, so this only affects how fast
  // the search converges.
  if (best < 0 || !(best_score > 0.0)) {
    return BranchLargestFirst(box, active_set, left, right, ur);
  }
  std::pair<Box, Box> bisected{box.bisect(best)};  // midpoint
  *left = std::move(bisected.first);
  *right = std::move(bisected.second);
  return best;
}

}  // namespace dreal
