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
#include "dreal/solver/context_impl.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <dreal/symbolic/prefix_printer.h>
#include <dreal/symbolic/symbolic_formula_cell.h>

#include <fmt/format.h>

#include "dreal/util/pattern_matching/substitutions_map.h"
#include "dreal/version.h"
#include "dreal/solver/auditor.h"
#include "dreal/solver/filter_assertion.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/if_then_else_eliminator.h"
#include "dreal/util/interrupt.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/rounding.h"

namespace dreal {

using std::find_if;
using std::isfinite;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::unordered_set;
using std::vector;

namespace {
// It is possible that a solution box has a dimension whose diameter
// is larger than delta when the given constraints are not tight.
// This function tighten the box @p box so that every dimension has a
// width smaller than delta.
void Tighten(Box* box, const double delta) {
  // This runs as delta-sat post-processing from Context::Impl::CheckSat(),
  // whose ambient FPU mode is undefined (whatever CheckSatCore left — and once
  // CAPD is linked, that is FE_TONEAREST). safe_diam/safe_mid and the gaol `&=`
  // below are all gaol computations sound only under FE_UPWARD, so establish it
  // for the whole pass and mint the token the directed-rounding helpers need.
  const UpwardRoundingScope round_scope;
  const UpwardRounding ur{round_scope.token()};
  for (int i = 0; i < box->size(); ++i) {
    auto& interval = (*box)[i];
    if (safe_diam(interval, ur) > delta) {
      const Variable& var{box->variable(i)};
      switch (var.get_type()) {
        case Variable::Type::BINARY:
        case Variable::Type::BOOLEAN:
          // Always pick True (1.0)
          interval = 1.0;
          break;
        case Variable::Type::CONTINUOUS: {
          // Sound outward-rounded [mid - delta/2, mid + delta/2]. The previous
          // hand-built `Box::Interval(mid - half, mid + half)` was mis-rounded
          // under every single rounding mode (the lower endpoint pulled inward,
          // yielding a too-narrow box). The typed directed-rounding helpers make
          // the outward rounding explicit and compiler-checked; see
          // util/rounded_interval.h.
          const Exact mid{safe_mid(interval, ur)};
          const Exact half_delta{Exact{delta}.half()};
          interval &= make_sound_interval(sub_down(mid, half_delta, ur),
                                          add_up(mid, half_delta, ur));
        } break;
        case Variable::Type::INTEGER: {
          // static_cast<int>(double) truncates toward zero independent of the
          // rounding mode; only the safe_mid() is FE_UPWARD-sensitive.
          interval = static_cast<int>(safe_mid(interval, ur));
        } break;
      }
    }
  }
}

bool ParseBooleanOption(const string& key, const string& val) {
  if (val == "true") {
    return true;
  }
  if (val == "false") {
    return false;
  }
  throw DREAL_RUNTIME_ERROR("Unknown value {} is provided for option {}", val,
                            key);
}
}  // namespace

Context::Impl::Impl() : Impl{Config{}} {}

Context::Impl::Impl(Config config)
    : config_{std::move(config)},
      sat_solver_{config_},
      theory_solver_{config_} {
  boxes_.push_back(Box{});
}

void Context::Impl::Assert(const Formula& f) {
  if (is_true(f)) {
    return;
  }
  if (box().empty()) {
    return;
  }
  if (is_false(f)) {
    box().set_empty();
    return;
  }
  if (is_conjunction(f)) {  // because otherwise FilterAssertion may miss some box updates!
    for (const auto& operand : get_operands(f)) Assert(operand);
    return;
  }
  // if (true) {
  if (!FilterAssertion(f, &box()).filtered) {
    DREAL_LOG_DEBUG("ContextImpl::Assert: {} is added.", f);
    IfThenElseEliminator ite_eliminator;
    const Formula no_ite{ite_eliminator.Process(f)};
    for (const Variable& ite_var : ite_eliminator.variables()) {
      // Note that the following does not mark `ite_var` as a model variable.
      AddToBox(ite_var);
    }
    const Formula normalized{pn_.Convert(no_ite)};
    stack_.push_back(normalized);
    sat_solver_.AddFormula(normalized);
    return;
  } else {
    DREAL_LOG_DEBUG("ContextImpl::Assert: {} is not added.", f);
    DREAL_LOG_DEBUG("Box=\n{}", box());
    return;
  }
}

void drpm_benchmark_log(
  const bool fully_constrained_model,
  const double sat_solve_elapsed_ms,
  const double theory_solve_elapsed_ms,
  const unsigned lemma_size,
  const char mode,
  const double pm_elapsed_ms,
  const unsigned num_pm,
#if CAV26_FILTER_SYMMETRIES
  const unsigned CAV26_pm_num_not_pure_time,
  const unsigned CAV26_pm_num_not_pure_logic,
  const unsigned CAV26_pm_num_not_pure_any,
#endif
  std::ostream& out
) {
  out << ".\t";
  out << " S.FCM " << fully_constrained_model;
  out << " S.ms " << sat_solve_elapsed_ms;
  out << " T.ms " << theory_solve_elapsed_ms;
  out << '\t';
  out << " L " << lemma_size;
  out << ' ' << mode;
  out << "\t";
  out << " PM.ms " << pm_elapsed_ms;
  out << " PM " << num_pm;
#if CAV26_FILTER_SYMMETRIES
  out << '\t';
  out << " C26.npT " << CAV26_pm_num_not_pure_time;
  out << " C26.npL " << CAV26_pm_num_not_pure_logic;
  out << " C26.np* " << CAV26_pm_num_not_pure_any;
#endif
  out << '\n';
}

optional<Box> Context::Impl::CheckSatCore(const ScopedVector<Formula>& stack,
                                          Box box,
                                          SatSolver* const sat_solver) {
  DREAL_LOG_DEBUG("ContextImpl::CheckSatCore()");
  DREAL_LOG_TRACE("ContextImpl::CheckSat: Box =\n{}", box);
  if (box.empty()) return {};
  // If false ∈ stack, it's UNSAT.
  for (const auto& f : stack.get_vector()) if (is_false(f)) return {};
  // If stack = ∅ or stack = {true}, it's trivially SAT.
  if (stack.empty() || (stack.size() == 1 && is_true(stack.first()))) {
    DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Found Model\n{}", box);
    return box;
  }
  DREAL_LOG_INFO("Initialized. Beginning SAT <=> Theory cycles.");
  std::cerr << std::setprecision(3);
  while (true) {
    // Note that 'DREAL_CHECK_INTERRUPT' is only defined in setup.py,
    // when we build dReal python package.
#ifdef DREAL_CHECK_INTERRUPT
    if (g_interrupted) {
      DREAL_LOG_DEBUG("KeyboardInterrupt(SIGINT) Detected.");
      throw std::runtime_error("KeyboardInterrupt(SIGINT) Detected.");
    }
#endif

    const bool request_fully_constrained =
      DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS ||
      recent_under_constrained_deltasat > 0;

    /* ################################ START MEASURING TIME ################################ */
    const auto scs_start = std::chrono::high_resolution_clock::now();
    const auto optional_model_and_fully_constrained = sat_solver->CheckSat(request_fully_constrained);
    const auto scs_end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double, std::milli> scs_elapsed_ms = scs_end - scs_start;
    /* ################################ STOP MEASURING TIME ################################ */

    if (optional_model_and_fully_constrained) {
      const auto& [optional_model, is_full_constrained] = *optional_model_and_fully_constrained;
      if (request_fully_constrained) DREAL_ASSERT(is_full_constrained);
      if (DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS) DREAL_ASSERT(request_fully_constrained && is_full_constrained);

      const vector<pair<Variable, bool>>& boolean_model{optional_model.first};
      const vector<pair<Variable, bool>>& theory_model{optional_model.second};

      for (const auto& [sat_var, assignment] : boolean_model)
        box[sat_var] = assignment ? 1.0 : 0.0;  // true -> 1.0 and false -> 0.0

      if (!theory_model.empty()) {
        // SAT from SATSolver.
        DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Sat Check = SAT");

        vector<Formula> assertions;
        assertions.reserve(theory_model.size());
        for (const auto& [sat_var, assignment] : theory_model)
          assertions.emplace_back(
            assignment ? sat_solver->theory_literal(sat_var) : !sat_solver->theory_literal(sat_var)
          );
        std::sort(assertions.begin(), assertions.end(), [](const Formula& a, const Formula& b) {
          return a.GetFreeVariables().size() < b.GetFreeVariables().size(); // ascending
        });

        /* ################################ START MEASURING TIME ################################ */
        const auto tcs_start = std::chrono::high_resolution_clock::now();
        const auto tscs_result = theory_solver_.CheckSat(box, assertions);
        const auto tcs_end = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double, std::milli> tcs_elapsed_ms = tcs_end - tcs_start;
        /* ################################ STOP MEASURING TIME ################################ */

        if (tscs_result && !is_full_constrained) {
          // SAT from TheorySolver.
          // the next 3 unsats should be fully constrained before we can try under constrained stuff again.
          // or, we get a fully constrained deltasat, in which case we are done :)
          recent_under_constrained_deltasat = recent_under_constrained_deltasat_limit;
          DREAL_LOG_WARN("ContextImpl::CheckSatCore() - Underconstrained Theory Check = delta-SAT. Exponential Backoff = {}", recent_under_constrained_deltasat_limit);
          // todo: log scs_elapsed_ms, tcs_elapsed_ms
          recent_under_constrained_deltasat_limit *= 2; // exponential backoff
          return CheckSatCore(stack, std::move(box), sat_solver);
        } else if (tscs_result && is_full_constrained) {
          DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Fully Constrained Theory Check = delta-SAT");
          return theory_solver_.GetModel();
        } else {
          // UNSAT from TheorySolver.
          if (recent_under_constrained_deltasat > 0) recent_under_constrained_deltasat--;

          DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Theory Check = UNSAT");
          std::vector explanation(
            theory_solver_.GetExplanation().begin(),
            theory_solver_.GetExplanation().end()
          );
          DREAL_LOG_DEBUG(
              "ContextImpl::CheckSatCore() - size of explanation = {} - stack size = {}",
              explanation.size(), stack.get_vector().size());

          // ordering the literals like this makes pattern matching fast.
          // todo: abstract this away better. should not happen at the top-level like it is now.
          std::sort(explanation.begin(), explanation.end(), [](const Formula &a, const Formula &b) {
              return a.GetFreeVariables().size() > b.GetFreeVariables().size(); // descending
          });

          if (explanation.size() < config().drpm_max_size()
            /*&& explanation.size() < 384 /* stack overflows around size=960 on x86 #1#
            && tscs_elapsed > std::chrono::milliseconds(3)*/) {
            /* ################################ START MEASURING TIME ################################ */
#define TO_MICROS(x) ( std::chrono::duration_cast<std::chrono::microseconds>((x)) )
            auto pm_timeout = TO_MICROS(scs_elapsed_ms + tcs_elapsed_ms);
            pm_timeout += std::min( // for edge-case of very, very long-running lemmas. try HARD to match those.
              99 * pm_timeout, // `+=`, so 100
              TO_MICROS(config().drpm_max_time())
            );
            const auto pm_start = std::chrono::high_resolution_clock::now();
            const auto pm_result = sat_solver->AddLearnedClausePattern(pn_, explanation, box, pm_timeout);
            const auto pm_end = std::chrono::high_resolution_clock::now();
            const std::chrono::duration<double, std::milli> pm_elapsed = pm_end - pm_start;
            /* ################################ STOP MEASURING TIME ################################ */

            // The pattern matching may time out. At the very minimum, make sure the original at least gets inserted.
            sat_solver->AddLearnedClauseDirect(explanation, box);

            drpm_benchmark_log(
              is_full_constrained, scs_elapsed_ms.count(), tcs_elapsed_ms.count(), explanation.size(),
              'M',
              pm_elapsed.count(), pm_result.matches,
#if CAV26_FILTER_SYMMETRIES
              pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_TIME),
              pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_LOGIC),
              pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_ANY),
#endif
              std::cerr
            );
          }
          else {
            if (DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED) pm_dump_all(explanation, {}, {}, {});
            sat_solver->AddLearnedClauseDirect(explanation, box);
            drpm_benchmark_log(
              is_full_constrained, scs_elapsed_ms.count(), tcs_elapsed_ms.count(), explanation.size(),
              'A',
              0 /*pm_elapsed.count()*/, 0 /*pm_result.matches*/,
#if CAV26_FILTER_SYMMETRIES
              0 /*pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_TIME)*/,
              0 /*pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_LOGIC)*/,
              0 /*pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_ANY)*/,
#endif
              std::cerr
            );
          }

          if (DREAL_LOG_TRACE_ENABLED) {
            for (const auto& f_i : stack.get_vector())
              DREAL_LOG_TRACE("ContextImpl::CheckSatCore: Stack {}", f_i);
            for (const auto& f_i : explanation)
              DREAL_LOG_TRACE("ContextImpl::CheckSatCore: Explanation {}", f_i);
          }
        }
      } else /* theory_model.empty() */ return box;
    } else /* !optional_model */ {
      // UNSAT from SATSolver. Escape the loop.
      DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Sat Check = UNSAT");
      return {};
    }
  }
}

optional<Box> Context::Impl::CheckSat() {
  auto result = CheckSatCore(stack_, box(), &sat_solver_);
  if (result) {
    // In case of delta-sat, do post-processing.
    Tighten(&(*result), config_.precision());
    DREAL_LOG_DEBUG("ContextImpl::CheckSat() - Found Model\n{}", *result);
    model_ = ExtractModel(*result);
    return model_;
  } else {
    model_.set_empty();
    return result;
  }
}

void Context::Impl::AddToBox(const Variable& v) {
  DREAL_LOG_TRACE("ContextImpl::AddToBox({})", fmt::streamed(v));
  const auto& variables = box().variables();
  if (find_if(variables.begin(), variables.end(), [&v](const Variable& v_) {
        return v.equal_to(v_);
      }) == variables.end()) {
    // v is not in box.
    box().Add(v);
  }
}

void Context::Impl::DeclareVariable(const Variable& v,
                                    const bool is_model_variable) {
  DREAL_LOG_TRACE("ContextImpl::DeclareVariable({})", fmt::streamed(v));
  AddToBox(v);
  if (is_model_variable) {
    mark_model_variable(v);
  }
}

void Context::Impl::SetDomain(const Variable& v, const Expression& lb,
                              const Expression& ub) {
  const double lb_fp =
      is_real_constant(lb) ? get_lb_of_real_constant(lb) : lb.Evaluate();
  const double ub_fp =
      is_real_constant(ub) ? get_ub_of_real_constant(ub) : ub.Evaluate();
  SetInterval(v, lb_fp, ub_fp);
}

void Context::Impl::Minimize(const vector<Expression>& functions) {
  // Given objective functions f₁(x), ... fₙ(x) and the current
  // constraints ϕᵢ which involves x. this method encodes them into a
  // universally quantified formula ψ:
  //
  //    ψ = ∀y. (⋀ᵢ ϕᵢ(y)) → (f₁(x) ≤ f₁(y) ∨ ... ∨ fₙ(x) ≤ fₙ(y))
  //      = ∀y. ¬(⋀ᵢ ϕᵢ(y)) ∨ (f₁(x) ≤ f₁(y) ∨ ... ∨ fₙ(x) ≤ fₙ(y))
  //      = ∀y. (∨ᵢ ¬ϕᵢ(y)) ∨ (f₁(x) ≤ f₁(y) ∨ ... ∨ fₙ(x) ≤ fₙ(y)).
  //
  // Here we introduce existential variables zᵢ for fᵢ(x) to have
  //
  //    ψ =  ∃z₁...zₙ. (z₁ = f₁(x) ∧ ... ∧ zₙ = fₙ(x)) ∧
  //              [∀y. (∨ᵢ ¬ϕᵢ(y)) ∨ (z₁ ≤ f₁(y) ∨ ... ∨ zₙ ≤ fₙ(y))].
  //
  // Note that when we have more than one objective function, this
  // encoding scheme denotes Pareto optimality.
  //
  // To construct ϕᵢ(y), we need to traverse both `box()` and
  // `stack_` because some of the asserted formulas are translated
  // and applied into `box()`.
  set<Formula> set_of_negated_phi;  // Collects ¬ϕᵢ(y).
  ExpressionSubstitution subst;  // Maps xᵢ ↦ yᵢ to build f(y₁, ..., yₙ).
  Variables quantified_variables;  // {y₁, ... yₙ}
  Variables x_vars;
  for (const Expression& f : functions) {
    x_vars += f.GetVariables();
  }

  // Collects side-constraints related to the cost functions.
  unordered_set<Formula> constraints;
  bool keep_going = true;
  while (keep_going) {
    keep_going = false;
    for (const Formula& constraint : stack_) {
      if (constraints.find(constraint) == constraints.end() &&
          HaveIntersection(x_vars, constraint.GetFreeVariables())) {
        x_vars += constraint.GetFreeVariables();
        constraints.insert(constraint);
        keep_going = true;
      }
    }
  }

  for (const Variable& x_i : x_vars) {
    // We add postfix '_' to name y_i
    const Variable y_i{x_i.get_name() + "_", x_i.get_type()};
    quantified_variables.insert(y_i);
    subst.emplace(x_i, y_i);
    // If `box()[x_i]` has a finite bound, let's add that information in
    // set_of_negated_phi as a constraint on y_i.
    const Box::Interval& bound_on_x_i{box()[x_i]};
    const double lb_x_i{bound_on_x_i.lb()};
    const double ub_x_i{bound_on_x_i.ub()};
    if (isfinite(lb_x_i)) {
      set_of_negated_phi.insert(!(lb_x_i <= y_i));
    }
    if (isfinite(ub_x_i)) {
      set_of_negated_phi.insert(!(y_i <= ub_x_i));
    }
  }

  for (const Formula& constraint : constraints) {
    set_of_negated_phi.insert(!constraint.Substitute(subst));
  }

  Formula quantified{make_disjunction(set_of_negated_phi)};  // ∨ᵢ ¬ϕᵢ(y)
  Formula new_z_block;  // This will have (z₁ = f₁(x) ∧ ... ∧ zₙ = fₙ(x)).
  static int counter{0};
  for (const Expression& f_i : functions) {
    const Variable z_i{fmt::format("Z{}", counter++),
                       Variable::Type::CONTINUOUS};
    AddToBox(z_i);
    new_z_block = new_z_block && (z_i == f_i);
    quantified = quantified || (z_i <= f_i.Substitute(subst));
  }
  const Formula psi{new_z_block && forall(quantified_variables, quantified)};
  return Assert(psi);
}

void Context::Impl::Pop() {
  DREAL_LOG_DEBUG("ContextImpl::Pop()");
  stack_.pop();
  boxes_.pop();
  sat_solver_.Pop();
}

void Context::Impl::Push() {
  DREAL_LOG_DEBUG("ContextImpl::Push()");
  sat_solver_.Push();
  boxes_.push();
  boxes_.push_back(boxes_.last());
  stack_.push();
}

void Context::Impl::SetInfo(const string& key, const double val) {
  DREAL_LOG_DEBUG("ContextImpl::SetInfo({} ↦ {})", key, val);
  info_[key] = fmt::format("{}", val);
}

void Context::Impl::SetInfo(const string& key, const string& val) {
  DREAL_LOG_DEBUG("ContextImpl::SetInfo({} ↦ {})", key, val);
  info_[key] = val;
}

void Context::Impl::SetInterval(const Variable& v, const double lb,
                                const double ub) {
  DREAL_LOG_DEBUG("ContextImpl::SetInterval({} = [{}, {}])", fmt::streamed(v), lb, ub);
  box()[v] = Box::Interval(lb, ub);
}

void Context::Impl::SetLogic(const Logic& logic) {
  DREAL_LOG_DEBUG("ContextImpl::SetLogic({})", logic);
  logic_ = logic;
}

void Context::Impl::SetOption(const string& key, const double val) {
  DREAL_LOG_DEBUG("ContextImpl::SetOption({} ↦ {})", key, val);
  option_[key] = fmt::format("{}", val);

  if (key == ":precision") {
    if (val <= 0.0) {
      throw DREAL_RUNTIME_ERROR("Precision has to be positive (input = {}).",
                                val);
    }
    return config_.mutable_precision().set_from_file(val);
  }
}

optional<string> Context::Impl::GetOption(const string& key) const {
  DREAL_LOG_DEBUG("ContextImpl::GetOption({})");
  const auto it = option_.find(key);
  if (it != option_.end()) {
    return it->second;
  }
  // Handle :precision as a special case.
  if (key == ":precision") {
    return fmt::format("{}", config_.precision());
  }
  return nullopt;
}

void Context::Impl::SetOption(const string& key, const string& val) {
  DREAL_LOG_DEBUG("ContextImpl::SetOption({} ↦ {})", key, val);
  option_[key] = val;
  if (key == ":polytope") {
    return config_.mutable_use_polytope().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":forall-polytope" || key == "forall_polytope") {
    return config_.mutable_use_polytope_in_forall().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":local-optimization" || key == "local_optimization") {
    return config_.mutable_use_local_optimization().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":worklist-fixpoint" || key == ":worklist_fixpoint") {
    return config_.mutable_use_worklist_fixpoint().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":produce-models" || key == ":produce_models") {
    return config_.mutable_produce_models().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":visualize") {
    return config_.mutable_visualize().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":smtlib2-compliant" || key == ":smtlib2_compliant") {
    return config_.mutable_smtlib2_compliant().set_from_file(
        ParseBooleanOption(key, val));
  }
}

Box Context::Impl::ExtractModel(const Box& box) const {
  if (static_cast<int>(model_variables_.size()) == box.size()) {
    // Every variable is a model variable. Simply return the @p box.
    return box;
  }
  Box new_box;
  for (const Variable& v : box.variables()) {
    if (is_model_variable(v)) {
      new_box.Add(v, box[v].lb(), box[v].ub());
    }
  }
  return new_box;
}

bool Context::Impl::is_model_variable(const Variable& v) const {
  return (model_variables_.find(v.get_id()) != model_variables_.end());
}

void Context::Impl::mark_model_variable(const Variable& v) {
  model_variables_.insert(v.get_id());
}

const ScopedVector<Formula>& Context::Impl::assertions() const {
  return stack_;
}

}  // namespace dreal
