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
#include <utility>

#include <dreal/util/rounded_interval.h>

#include "dreal/solver/brancher.h"  // BranchLargestFirstWithRatio
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/assert.h"
#include "dreal/util/logging.h"

namespace dreal {

using std::make_unique;
using std::unique_ptr;
using std::vector;

SmearBrancher::SmearBrancher(
    const vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    const double ratio)
    : ibex_converter_{box}, ratio_{ratio} {
  // Assemble an ibex::System over the box variables and the relational
  // constraints (skip forall / ODE — the smear Jacobian is for plain NRA
  // constraints). Same assembly as ContractorIbexPolytope/Acid; built from the
  // box so the System's variable order == box-index order.
  system_factory_ = make_unique<ibex::SystemFactory>();
  system_factory_->add_var(ibex_converter_.variables());
  int n_ctr = 0;
  for (const FormulaEvaluator& fe : formula_evaluators) {
    const Formula& f = fe.formula();
    if (is_forall(f) || f.include_ode()) continue;
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

int SmearBrancher::operator()(const Box& box, const DynamicBitset& active_set,
                              Box* const left, Box* const right,
                              const UpwardRounding& ur) const {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  DREAL_ASSERT(!active_set.none());
  if (is_dummy_) {
    return BranchLargestFirstWithRatio(box, active_set, left, right, ur, ratio_);
  }

  const Box::IntervalVector& iv{box.interval_vector()};
  const int n{static_cast<int>(box.size())};
  // Interval Jacobian (nb_ctr x n); column k aligns 1:1 with box dimension k
  // (the converter was built from the box). gaol AD -> needs FE_UPWARD (ur).
  const ibex::IntervalMatrix J{system_->f_ctrs.jacobian(iv)};

  vector<double> w(n, 0.0);
  for (int k = 0; k < n; ++k) w[k] = safe_diam(iv[k], ur);

  // smearsumrel: score_j = Σ_i |J[i][j]|·w[j] / NC_i, NC_i = Σ_k |J[i][k]|·w[k].
  vector<double> score(n, 0.0);
  for (int i = 0; i < system_->nb_ctr; ++i) {
    double nc = 0.0;
    for (int k = 0; k < n; ++k) nc += J[i][k].mag() * w[k];
    if (!(nc > 0.0) || !std::isfinite(nc)) continue;  // uninformative row
    for (int k = 0; k < n; ++k) score[k] += (J[i][k].mag() * w[k]) / nc;
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
    return BranchLargestFirstWithRatio(box, active_set, left, right, ur, ratio_);
  }
  std::pair<Box, Box> bisected{box.bisect(best, ratio_)};
  *left = std::move(bisected.first);
  *right = std::move(bisected.second);
  return best;
}

}  // namespace dreal
