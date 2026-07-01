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
#include "dreal/contractor/forall_counterexample_query.h"

#include <vector>

#include "dreal/solver/filter_assertion.h"
#include "dreal/util/box.h"
#include "dreal/util/nnfizer.h"
#include "dreal/util/optional.h"

namespace dreal {
namespace {

// If @p c is a relational bound atom on a single universal variable
// (`const relop v` or `v relop const`, v ∈ @p qvars), returns v; else nullopt.
// This is the `var relop const` shape the parser emits for binder bounds and
// that FilterAssertion recognizes.
optional<Variable> UniversalBoundVar(const Formula& c, const Variables& qvars) {
  if (!is_relational(c)) {
    return {};
  }
  const Expression& lhs{get_lhs_expression(c)};
  const Expression& rhs{get_rhs_expression(c)};
  const bool lhs_const{is_constant(lhs) || is_real_constant(lhs)};
  const bool rhs_const{is_constant(rhs) || is_real_constant(rhs)};
  if (is_variable(lhs) && rhs_const && qvars.include(get_variable(lhs))) {
    return get_variable(lhs);
  }
  if (is_variable(rhs) && lhs_const && qvars.include(get_variable(rhs))) {
    return get_variable(rhs);
  }
  return {};
}

}  // namespace

Formula StrengthenForallCounterexampleQuery(
    const Formula& matrix, const Variables& quantified_variables,
    const double epsilon) {
  // ¬matrix = domain ∧ ¬body, NNF so the binder bounds are top-level conjuncts.
  const Formula neg{Nnfizer{}.Convert(!matrix, true)};

  // Recover each universal variable's binder domain, to tell NARROW from WIDE.
  Box ubox{std::vector<Variable>{quantified_variables.begin(),
                                 quantified_variables.end()}};
  const auto absorb = [&ubox, &quantified_variables](const Formula& c) {
    if (c.GetFreeVariables().IsSubsetOf(quantified_variables)) {
      FilterAssertion(c, &ubox);
    }
  };
  if (is_conjunction(neg)) {
    for (const Formula& c : get_operands(neg)) {
      absorb(c);
    }
  } else {
    absorb(neg);
  }

  // A universal variable is NARROW when the ε-shrink would leave < ε of search
  // interior (width < 3ε) — the regime where shrinking the domain empties or
  // degenerates it and misses real counterexamples (the H1 hazard; false
  // `delta-sat`). Keep NARROW variables' domain bounds EXACT (the fix). WIDE
  // variables keep the old, faster ε-shrink — its cost is severe at moderate δ
  // and its residual completeness gap (the domain shell) is bounded, pre-existing,
  // and never a soundness issue (COMPLETENESS-only either way).
  Variables narrow;
  for (const Variable& y : quantified_variables) {
    const Box::Interval& iv{ubox[y]};
    if (iv.ub() - iv.lb() < 3.0 * epsilon) {
      narrow.insert(y);
    }
  }

  // Keep exact only the bound atoms of NARROW universal variables; ε-strengthen
  // everything else (WIDE-variable bounds and the negated body).
  Formula exact{Formula::True()};
  Formula rest{Formula::True()};
  const auto route = [&](const Formula& c) {
    const optional<Variable> v{UniversalBoundVar(c, quantified_variables)};
    if (v && narrow.include(*v)) {
      exact = exact && c;
    } else {
      rest = rest && c;
    }
  };
  if (is_conjunction(neg)) {
    for (const Formula& c : get_operands(neg)) {
      route(c);
    }
  } else {
    route(neg);
  }
  return exact && DeltaStrengthen(rest, epsilon);
}

}  // namespace dreal
