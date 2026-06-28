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
#include "dreal/contractor/counterexample_refiner.h"

#include <utility>

#include "dreal/solver/filter_assertion.h"
#include "dreal/util/assert.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/logging.h"

namespace dreal {

using std::make_unique;
using std::vector;

CounterexampleRefiner::CounterexampleRefiner(const Formula& query,
                                             Variables forall_variables,
                                             const Config& config)
    : init_(forall_variables.size(), 0.0),
      forall_variables_{std::move(forall_variables)} {
  // Build forall_vec_ (of vector<Variable>).
  for (const Variable& var : forall_variables_) {
    forall_vec_.push_back(var);
  }

  // 1. Filter bound constraints.
  Box box{forall_vec_};
  vector<Formula> formulas;
  if (is_conjunction(query)) {
    formulas.reserve(get_operands(query).size());
    for (const Formula& f : get_operands(query)) {
      DREAL_ASSERT(is_relational(f));
      const FilterAssertionResult result{FilterAssertion(f, &box)};
      if (!result.filtered) {
        formulas.push_back(f);
      }
    }
  } else {
    DREAL_ASSERT(is_relational(query));
    const FilterAssertionResult result{FilterAssertion(query, &box)};
    if (!result.filtered) {
      formulas.push_back(query);
    }
  }
  if (formulas.empty()) {
    // This will leave opt_ as nullptr. `Refine(box)` will return box then.
    DREAL_ASSERT(!opt_);
    return;
  }

  // 2. Build an Nlopt problem by adding constraints and setting up an
  // NloptOptimizer is a wrapper and guards rounding mode internally
  // objective function.
  if (IsDifferentiable(query)) {
    // See https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/#slsqp
    opt_ = make_unique<NloptOptimizer>(nlopt::algorithm::LD_SLSQP, box, config);
  } else {
    // See
    // http://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/#cobyla-constrained-optimization-by-linear-approximations
    opt_ =
        make_unique<NloptOptimizer>(nlopt::algorithm::LN_COBYLA, box, config);
  }
  Expression objective{};
  for (const Formula& f : formulas) {
    if (!f.GetFreeVariables().IsSubsetOf(forall_variables_)) {
      // F has both exist and forall variables. Fold its violation (0 for
      // equality/disequality) into the objective. ConstraintViolation is the
      // shared builder used by seed-and-verify too (see nlopt_optimizer.h).
      objective += ConstraintViolation(f);
    }
    // Always add it as a constraint.
    opt_->AddRelationalConstraint(f);
  }
  if (!is_zero(objective)) {
    opt_->SetMinObjective(objective);
  }
}

Box CounterexampleRefiner::Refine(Box box, const UpwardRounding& ur) {
  if (!opt_) {
    return box;
  }

  // 1. Set up env (exist variables) and init (forall variables).
  //    NloptOptimizer indexes its dimensions by Box{forall_vec_} order, so
  //    init_[k] must hold forall_vec_[k]'s value. Keying both the initial guess
  //    here and the read-back below off forall_vec_ position (not a counter that
  //    advances over box-insertion order) keeps them aligned with nlopt and with
  //    each other regardless of how the box's variables were ordered.
  Environment env;
  for (const Variable& var : box.variables()) {
    if (!forall_variables_.include(var)) {
      env.insert(var, safe_mid(box[var], ur));  // exist variable
    }
  }
  for (int k = 0; k < static_cast<int>(forall_vec_.size()); ++k) {
    init_[k] = safe_mid(box[forall_vec_[k]], ur);
  }
  // 2. call optimizer
  double optimal_value{0.0};
  try {
    const nlopt::result result = opt_->Optimize(&init_, &optimal_value, env); // already rounding-guarded
    switch (result) {
      case nlopt::result::FAILURE:
        DREAL_LOG_ERROR("LOCAL OPT FAILED: nlopt error-code {}", "FAILURE");
        break;
      case nlopt::result::INVALID_ARGS:
        DREAL_LOG_ERROR("LOCAL OPT FAILED: nlopt error-code {}",
                        "INVALID_ARGS");
        break;
      case nlopt::result::OUT_OF_MEMORY:
        DREAL_LOG_ERROR("LOCAL OPT FAILED: nlopt error-code {}",
                        "OUT_OF_MEMORY");
        break;
      case nlopt::result::FORCED_STOP:
        DREAL_LOG_ERROR("LOCAL OPT FAILED: nlopt error-code {}", "FORCED_STOP");
        break;
      case nlopt::result::SUCCESS:
      case nlopt::result::STOPVAL_REACHED:
      case nlopt::result::FTOL_REACHED:
      case nlopt::result::XTOL_REACHED:
      case nlopt::result::MAXEVAL_REACHED:
      case nlopt::result::MAXTIME_REACHED:
      case nlopt::result::ROUNDOFF_LIMITED:
        // 3. move the solution values from x into box (same forall_vec_ order
        //    nlopt and the init_ setup above use).
        for (int k = 0; k < static_cast<int>(forall_vec_.size()); ++k) {
          box[forall_vec_[k]] = init_[k];
        }
        break;
      default:
        DREAL_LOG_ERROR("LOCAL OPT FAILED: Unknown nlopt error-code {}",
                        fmt::streamed(result));
        break;
    }
    return box;
  } catch (std::exception& e) {
    DREAL_LOG_DEBUG("LOCAL OPT FAILED: Exception {}", e.what());
    return box;
  }
}
}  // namespace dreal
