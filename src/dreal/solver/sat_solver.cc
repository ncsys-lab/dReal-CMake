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
#include "dreal/solver/sat_solver.h"

#include <ostream>
#include <utility>
#include <dreal/util/predicate_normalizer.h>

#include "auditor.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"

namespace dreal {

using std::cout;
using std::set;
using std::vector;

SatSolver::SatSolver(const Config& config) : cadical(new CaDiCaL::Solver) {
  // SatSolver::CheckSat().
  if (config.random_seed() != 0) {
    cadical->set("seed", config.random_seed());
    DREAL_LOG_DEBUG("SatSolver::Set Random Seed {}", config.random_seed());
  }
  // this actually doesn't work lol.
  // cadical->set("phase", static_cast<int>(config.sat_default_phase()));
  // DREAL_LOG_DEBUG("SatSolver::Set Default Phase {}",
                  // config.sat_default_phase());

  // todo: look into this? want to absolutely minimize calls to theory solver.
  // eliminate as many variables as possible. ?
  cadical->optimize(9);
  cadical->set("condition", 1); // "globally blocked clause elim"
  cadical->set("cover", 1); // "covered clause elimination"
  cadical->set("block", 1); // "blocked clause elimination"
}

SatSolver::~SatSolver() { delete cadical; }

void SatSolver::AddFormula(const Formula& f) {
  DREAL_LOG_DEBUG("SatSolver::AddFormula({})", f);
  vector<Formula> clauses{cnfizer_.Convert(f)};
  // Collect Tseitin variables.
  for (const auto& p : cnfizer_.map()) {
    tseitin_variables_.insert(p.first.get_id());
  }
  for (Formula& clause : clauses) {
    AddClause(predicate_abstractor_.Convert(clause));
  }
}

void SatSolver::AddLearnedClause(const vector<Formula>& conflicting_conjunction, const Box& box) {
  if (DREAL_LOG_DEBUG_ENABLED) audit(!make_conjunction(conflicting_conjunction), box);
  for (const Formula& f : conflicting_conjunction) {
    AddLiteral(!predicate_abstractor_.Convert(f));
  }
  cadical->add(0);
}

void SatSolver::AddBox(PredicateNormalizer& pn, const Box& base_box) {
  for (const auto& v : base_box.variables()) {
    if (v.get_type() == Variable::Type::BOOLEAN) continue;
    const auto condition = MakeSatIntervalVar(pn, v, base_box[v]);
    if (is_true(condition)) continue;
    for (
      const auto& lit :
      is_conjunction(condition) ? get_operands(condition) : set{condition}
    ) {
      // todo: audit? currently can't because it is already predicate_abstractor-ed
      AddLiteral(lit);
      cadical->add(0);
    }
  }
}

PatternMatchingTrie::matching_stats_t SatSolver::AddLearnedClausePattern(
  PredicateNormalizer& pn,
  const vector<Formula>& base_conflict, const Box& base_box,
  const std::chrono::duration<uint64_t, std::micro> timeout
) {
  // the pattern matching is kinda best-effort now.
  // it breaks down when one clause pattern matches into 1000s of permutations of itself
  // just make the best effort, and at the very minimum make sure the original at least gets inserted
  AddLearnedClause(base_conflict, base_box);
  const auto [
    all_related_conflicts,
    match_statistics
    ] = pn.FindSimilar(base_conflict, timeout);
  if (all_related_conflicts.empty()) {
    DREAL_LOG_ERROR("Clause did not match with itself... Adding regularly.");
    return match_statistics;
  }

  /*
  for (const auto& [conflict_clause, subs] : all_related_conflicts) {
    Box conflict_box = substitutions_map::apply_substitution(base_box, subs, false);

    if (DREAL_LOG_DEBUG_ENABLED) { // todo: better gate.
      std::set conflict_clause_set(conflict_clause.begin(), conflict_clause.end());
      audit(!make_conjunction_SKIP_CHECKS_KUNAL_HACK(std::move(conflict_clause_set)), conflict_box);
    }

    // a & b & c & ... ==> ~(x & y & z & ...)
    // ~(a & b & c & ...) | ~(x & y & z & ...)
    // ~a | ~b | ~c | ... | ~x | ~y | ~z | ...

    for (const auto& v : conflict_box.variables()) {
      // if (v.get_type() == Variable::Type::BOOLEAN) continue; // todo: commented, because maybe unsound????
      const auto condition = MakeSatIntervalVar(pn, v, conflict_box[v]);
      if (is_true(condition)) continue;
      else if (!is_conjunction(condition)) AddLiteral(!condition); // already predicate-converted.
      else for (const auto& lit : get_operands(condition)) AddLiteral(!lit);
    }
    // ==>
    for (const Formula& f : conflict_clause) AddLiteral(!predicate_abstractor_.Convert(f));

    cadical->add(0);
  }
  */
  return match_statistics;
}

void SatSolver::AddClause(const Formula& f) {
  DREAL_LOG_DEBUG("SatSolver::AddClause({})", f);
  // Set up Variable ⇔ Literal (in SAT) map.
  for (const Variable& var : f.GetFreeVariables()) {
    MakeSatVar(var);
  }
  // Add clauses to SAT solver.
  if (is_disjunction(f)) {
    // f = l₁ ∨ ... ∨ lₙ
    for (const Formula& l : get_operands(f)) {
      AddLiteral(l);
    }
  } else {
    // f = b or f = ¬b.
    AddLiteral(f);
  }
  cadical->add(0);
}

namespace {
class SatSolverStat : public Stat {
 public:
  explicit SatSolverStat(const bool enabled) : Stat{enabled} {};
  SatSolverStat(const SatSolverStat&) = default;
  SatSolverStat(SatSolverStat&&) = default;
  SatSolverStat& operator=(const SatSolverStat&) = delete;
  SatSolverStat& operator=(SatSolverStat&&) = delete;
  ~SatSolverStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n", "Total # of CheckSat",
            "SAT level", num_check_sat_);
      print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
            "Total time spent in SAT checks", "SAT level",
            timer_check_sat_.seconds());
    }
  }

  int num_check_sat_{0};
  Timer timer_check_sat_;
};
}  // namespace

optional<SatSolver::Model> SatSolver::CheckSat() {
  static SatSolverStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_LOG_DEBUG("SatSolver::CheckSat(#vars = {}, #clauses = {})",
                  cadical->vars(),
                  cadical->irredundant());
  stat.num_check_sat_++;
  // Call SAT solver.
  TimerGuard check_sat_timer_guard(&stat.timer_check_sat_,
                                   DREAL_LOG_INFO_ENABLED);
  const int ret{cadical->solve()};
  // check_sat_timer_guard.pause();

  Model model;
  if (ret == CaDiCaL::SATISFIABLE) {
    // SAT Case.
    const auto& var_to_formula_map = predicate_abstractor_.var_to_formula_map();
    // from CaDiCaL documentation:
    //    try to avoid mixing 'flip' and 'val' (for efficiency only).
    std::vector<int> model_is(cadical->vars()+1);
    for (int i = 1; i <= cadical->vars(); ++i) model_is[i] = cadical->val(i) > 0 ? +1 : -1;
    for (int i = 1; i <= cadical->vars(); ++i) if(cadical->flip(i)) model_is[i] = 0; // todo: use IPASIR-UP, see cvc5 paper.
    // for (int i = 1; i <= cadical->vars(); ++i) if(model_is[i] == 0) cadical->flip(i); // restore default state.
    for (int i = 1; i <= cadical->vars(); ++i) {
      const auto model_i = model_is[i];
      if (model_i == 0) {
        continue;
      }
      const auto it_var = to_sym_var_.find(i);
      if (it_var == to_sym_var_.end()) {
        // There is no symbolic::Variable corresponding to this
        // picosat variable (int). This could be because of
        // picosat_push/pop.
        continue;
      }
      const Variable& var{it_var->second};
      const auto it = var_to_formula_map.find(var);
      if (it != var_to_formula_map.end()) {
        DREAL_LOG_TRACE("SatSolver::CheckSat: Add theory literal {}{} to Model",
                        model_i == 1 ? "" : "¬", fmt::streamed(var));
        auto& theory_model = model.second;
        theory_model.emplace_back(var, model_i == 1);
      } else if (tseitin_variables_.count(var.get_id()) == 0) {
        DREAL_LOG_TRACE(
            "SatSolver::CheckSat: Add Boolean literal {}{} to Model ",
            model_i == 1 ? "" : "¬", fmt::streamed(var));
        auto& boolean_model = model.first;
        boolean_model.emplace_back(var, model_i == 1);
      } else {
        DREAL_LOG_TRACE(
            "SatSolver::CheckSat: Skip {}{} which is a temporary variable.",
            model_i == 1 ? "" : "¬", fmt::streamed(var));
      }
    }
    DREAL_LOG_DEBUG("SatSolver::CheckSat() Found a model.");
    return model;
  } else if (ret == CaDiCaL::UNSATISFIABLE) {
    DREAL_LOG_DEBUG("SatSolver::CheckSat() No solution.");
    // UNSAT Case.
    return {};
  } else {
    DREAL_ASSERT(ret == CaDiCaL::UNKNOWN);
    DREAL_LOG_CRITICAL("CaDiCaL returns CaDiCaL::UNKNOWN.");
    throw DREAL_RUNTIME_ERROR("CaDiCaL returns CaDiCaL::UNKNOWN.");
  }
}

void SatSolver::Pop() {
  DREAL_LOG_DEBUG("SatSolver::Pop()");
  tseitin_variables_.pop();
  to_sym_var_.pop();
  to_sat_var_.pop();
  // picosat_pop(sat_);
  throw DREAL_RUNTIME_ERROR("NOT YET IMPLEMENTED SatSolver::Pop()");
  has_picosat_pop_used_ = true;
}

void SatSolver::Push() {
  DREAL_LOG_DEBUG("SatSolver::Push()");
  // picosat_push(sat_);
  throw DREAL_RUNTIME_ERROR("NOT YET IMPLEMENTED SatSolver::Push()");
  to_sat_var_.push();
  to_sym_var_.push();
  tseitin_variables_.push();
}

void SatSolver::AddLiteral(const Formula& f) {
  DREAL_ASSERT(is_variable(f) ||
               (is_negation(f) && is_variable(get_operand(f))));
  if (is_variable(f)) {
    // f = b
    const Variable& var{get_variable(f)};
    DREAL_ASSERT(var.get_type() == Variable::Type::BOOLEAN);
    // Add l = b
    cadical->add(to_sat_var_[var.get_id()]);
  } else {
    // f = ¬b
    DREAL_ASSERT(is_negation(f) && is_variable(get_operand(f)));
    const Variable& var{get_variable(get_operand(f))};
    DREAL_ASSERT(var.get_type() == Variable::Type::BOOLEAN);
    // Add l = ¬b
    cadical->add(-to_sat_var_[var.get_id()]);
  }
}

void SatSolver::MakeSatVar(const Variable& var) {
  auto it = to_sat_var_.find(var.get_id());
  if (it != to_sat_var_.end()) {
    // Found.
    return;
  }
  // It's not in the maps, let's make one and add it.
  const int sat_var{cadical_next_var++};
  // std::cout << "Assigning `" << var << "` (id #"<< var.get_id() <<") to " << sat_var << std::endl;
  to_sat_var_.insert(var.get_id(), sat_var);
  to_sym_var_.insert(sat_var, var);
  DREAL_LOG_DEBUG("SatSolver::MakeSatVar({} ↦ {})", fmt::streamed(var), sat_var);
}

Formula SatSolver::MakeSatIntervalVar(PredicateNormalizer &pn, const Variable& var, const Box::Interval& intv) {
  DREAL_ASSERT(var.get_type() != Variable::Type::BOOLEAN);
  DREAL_LOG_DEBUG("SatSolver::MakeSatIntervalVar({} ∈ {})", fmt::streamed(var), fmt::streamed(intv));
  auto ub_pred = Formula::True();
  if (isfinite(intv.ub())) {
    ub_pred = var <= intv.ub(); // TODO: figure out if this is inclusive or exclusive.
    // an iterator pointing to the first element that is greater or equal to intv.ub()
    auto& var_ub_preds = all_ub_predicates[var];
    auto ub_gte_it = var_ub_preds.lower_bound(intv.ub());
    if (!var_ub_preds.empty() && ub_gte_it->first == intv.ub()) ub_pred = ub_gte_it->second;
    else {
      ub_pred = predicate_abstractor_.Convert(pn.Convert(ub_pred));
      MakeSatVar(get_variable(ub_pred));

      if (ub_gte_it != var_ub_preds.end()) {
        const auto& gt = *ub_gte_it;
        DREAL_ASSERT(intv.ub() < gt.first);
        DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", ub_pred, gt.second);
        // std::cout << ub_pred << " => " << gt.second << std::endl;
        // (x < Ub) => (x < Ub+ε)
        // = ~(x < Ub) \/ (x < Ub+ε)
        // = ~((x < Ub) /\ ~(x < Ub+ε))
        AddLearnedClause({ub_pred, !gt.second}, {});
      }
      if (ub_gte_it != var_ub_preds.begin()) {
        --ub_gte_it;
        const auto& lt = *ub_gte_it;
        DREAL_ASSERT(lt.first < intv.ub());
        DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", lt.second, ub_pred);
        // std::cout << lt.second << " => " << ub_pred << std::endl;
        // (x < Ub-ε) => (x < Ub)
        // = ~(x < Ub-ε) \/ (x < Ub)
        // = ~((x < Ub-ε) /\ ~(x < Ub))
        AddLearnedClause({lt.second, !ub_pred}, {});
      }
      var_ub_preds[intv.ub()] = ub_pred;
    }
  }

  auto lb_pred = Formula::True();
  if (isfinite(intv.lb())) {
    lb_pred = intv.lb() <= var; // TODO: figure out if this is inclusive or exclusive.
    // an iterator pointing to the first element that is greater or equal to intv.lb()
    auto& var_lb_preds = all_lb_predicates[var];
    auto lb_gte_it = var_lb_preds.lower_bound(intv.lb());
    if (!var_lb_preds.empty() && lb_gte_it->first == intv.lb()) lb_pred = lb_gte_it->second;
    else {
      lb_pred = predicate_abstractor_.Convert(pn.Convert(lb_pred));
      MakeSatVar(get_variable(lb_pred));

      if (lb_gte_it != var_lb_preds.end()) {
        const auto& gt = *lb_gte_it;
        DREAL_ASSERT(intv.lb() < gt.first);
        DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", gt.second, lb_pred);
        // std::cout << gt.second << " => " << lb_pred << std::endl;
        // (Lb+ε < x) => (Lb < x)
        // = ~(Lb+ε < x) \/ (Lb < x)
        // = ~((Lb+ε < x) /\ ~(Lb < x))
        AddLearnedClause({gt.second, !lb_pred}, {});
      }
      if (lb_gte_it != var_lb_preds.begin()) {
        --lb_gte_it;
        const auto& lt = *lb_gte_it;
        DREAL_ASSERT(lt.first < intv.lb());
        DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", lb_pred, lt.second);
        // std::cout << lb_pred << " => " << lt.second << std::endl;
        // (Lb < x) => (Lb-ε < x)
        // = ~(Lb < x) \/ (Lb-ε < x)
        // = ~((Lb < x) /\ ~(Lb-ε < x))
        AddLearnedClause({lb_pred, !lt.second}, {});
      }
      var_lb_preds[intv.lb()] = lb_pred;
    }
  }

  return lb_pred && ub_pred;
}

Formula SatSolver::theory_literal(const Variable& var) const {
  return predicate_abstractor_[var];
}
}  // namespace dreal
