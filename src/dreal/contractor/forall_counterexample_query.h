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
#pragma once

#include "dreal/symbolic/symbolic.h"

namespace dreal {

/// Builds the counterexample-search query for the ∃∀ NRA `forall` whose
/// quantifier-free matrix (typically `domain → body`) is @p matrix, over the
/// universal variables @p quantified_variables, with strengthening margin
/// @p epsilon.
///
/// Returns the ε-strengthened negation of the matrix. Strengthening
/// `domain ∧ ¬body` as a monolith (the previous behavior) ε-tightened the
/// universal-domain bounds too, so a universal domain narrower than ~2ε was
/// searched as the EMPTY set, missing real counterexamples — a COMPLETENESS hole
/// (missed refutation → false `delta-sat`; never a false `unsat`).
///
/// The fix is applied CONDITIONALLY, per universal variable, to keep it cheap:
///  - NARROW variable (binder width < 3ε — the ε-shrink would empty or
///    degenerate its domain): keep its bounds EXACT (the CAV-2018 semantics,
///    where the domain is a hard bound on the CE search). Fixes the hazard.
///  - WIDE variable (width ≥ 3ε): keep the old, faster ε-shrink. Its residual
///    completeness gap (the domain shell) is bounded and pre-existing; keeping
///    exact here is correct too but 9–45× slower at moderate δ on wide-domain
///    ∃∀ families (odeexpr_v2), for no verdict change. COMPLETENESS-only either
///    way — soundness (false `unsat`) is never at stake.
///
/// Shared by both `ContractorForall` (the CEGIS prune loop) and
/// `ForallFormulaEvaluator` (the δ-stop test) so the two cannot drift. The
/// width==0 point case is handled upstream by point-quantifier elimination
/// (`Context::Assert`).
Formula StrengthenForallCounterexampleQuery(
    const Formula& matrix, const Variables& quantified_variables,
    double epsilon);

}  // namespace dreal
