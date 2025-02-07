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
  cadical->set("phase", static_cast<int>(config.sat_default_phase()));
  DREAL_LOG_DEBUG("SatSolver::Set Default Phase {}",
                  config.sat_default_phase());

  // todo: look into this? want to absolutely minimize calls to theory solver.
  cadical->optimize(4);
  cadical->set("block", 1);
}

SatSolver::SatSolver(const Config& config, const vector<Formula>& clauses)
    : SatSolver{config} {
  AddClauses(clauses);
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
    clause = predicate_abstractor_.Convert(clause);
  }
  AddClauses(clauses);
}

void SatSolver::AddFormulas(const vector<Formula>& formulas) {
  for (const Formula& f : formulas) {
    AddFormula(f);
  }
}

void SatSolver::AddLearnedClause(const set<Formula>& formulas) {
  for (const Formula& f : formulas) {
    AddLiteral(!predicate_abstractor_.Convert(f));
  }
  cadical->add(0);
}

void SatSolver::AddClauses(const vector<Formula>& formulas) {
  for (const Formula& f : formulas) {
    AddClause(f);
  }
}

void SatSolver::AddClause(const Formula& f) {
  DREAL_LOG_DEBUG("SatSolver::AddClause({})", f);
  // Set up Variable ⇔ Literal (in SAT) map.
  for (const Variable& var : f.GetFreeVariables()) {
    MakeSatVar(var);
  }
  // Add clauses to SAT solver.
  DoAddClause(f);
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

void SatSolver::DoAddClause(const Formula& f) {
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

void SatSolver::MakeSatVar(const Variable& var) {
  auto it = to_sat_var_.find(var.get_id());
  if (it != to_sat_var_.end()) {
    // Found.
    return;
  }
  // It's not in the maps, let's make one and add it.
  static int cadical_next_var = 1;
  const int sat_var{cadical_next_var++};

  to_sat_var_.insert(var.get_id(), sat_var);
  to_sym_var_.insert(sat_var, var);
  DREAL_LOG_DEBUG("SatSolver::MakeSatVar({} ↦ {})", fmt::streamed(var), sat_var);
}
}  // namespace dreal
