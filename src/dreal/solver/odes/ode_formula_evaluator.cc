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
#include "ode_formula_evaluator.h"

#include <algorithm>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/rounded_interval.h"

namespace dreal {

using std::ostream;

OdeFormulaEvaluator::OdeFormulaEvaluator(Formula f, const bool refine_witness)
    : FormulaEvaluatorCell{std::move(f)}, refine_witness_{refine_witness} {}

OdeFormulaEvaluator::~OdeFormulaEvaluator() {
  DREAL_LOG_TRACE("OdeFormulaEvaluator::~OdeFormulaEvaluator()");
}

FormulaEvaluationResult OdeFormulaEvaluator::operator()(
    const Box& box, const UpwardRounding& ur) const {
  // An ODE atom has no cheap interval evaluation — the tube contractor is the
  // sole refuter. Two regimes:
  //
  // Default (fast accept): report the atom satisfied as-is. ICP then accepts
  // delta-sat at tube granularity with no ODE-driven branching — load-bearing
  // for deep BMC, where the accepted box legitimately keeps most per-step
  // dims (dwell times, weakly-constrained states) wide: on water k32, 626 of
  // 703 dims were wider than δ at accept, and δ-refining them all costs 10³–
  // 10⁴ extra branch+fixpoint passes (A/B: 4.89× github PAR2, 18 SAT→TIM).
  // The cost of the fast accept: --model witnesses of un-pinned ODE dims are
  // unrefined-hull intervals, e.g. a free endpoint-time τ reported as
  // [0.375, 0.5] when the sole solution was 0.38 (the formerly unconditional
  // Tighten midpoint slice of that hull was BUG-011). This also applies to a
  // NEGATED ODE
  // literal (normal DPLL(T) product, unenforced by design — the documented §6
  // drop, docs/ode-integration.md BUG-002): branching it would enforce
  // nothing, in either regime.
  if (!refine_witness_ || is_negation(formula())) {
    return FormulaEvaluationResult{FormulaEvaluationResult::Type::VALID,
                                   Box::Interval(0.0, 0.0)};
  }
  // --refine-witness (δ-tight witnesses): report an interval whose
  // diameter is the widest ODE dimension, so EvaluateBox keeps the atom's
  // variables branching until every one is below δ — each split re-enters the
  // tube contractor, which prunes the wrong half. Use when --model values of
  // un-pinned ODE dims will be read (e.g. the free-τ crossing probes).
  double w{0.0};
  for (const Variable& v : variables()) {
    w = std::max(w, safe_diam(box[v], ur));
  }
  return FormulaEvaluationResult{FormulaEvaluationResult::Type::UNKNOWN,
                                 Box::Interval(0.0, w)};
}

ostream& OdeFormulaEvaluator::Display(ostream& os) const {
  return os << "OdeFormulaEvaluator(" << f_ << ")";
}
}  // namespace dreal
