//
// Created by Kunal Sheth on 4/17/25.
//

#include <dreal/util/logging.h>

#include "auditor.h"
#include "sat_solver.h"

namespace dreal {

using std::cout;
using std::set;
using std::vector;

void SatSolver::AddLearnedClause(
  PredicateNormalizer& pn,
  const vector<Formula>& conflicting_conjunction, const Box& box
) {
  if (DREAL_LOG_DEBUG_ENABLED) audit(!make_conjunction(conflicting_conjunction), box);

  // a & b & c & ... ==> ~(x & y & z & ...)
  // ~(a & b & c & ...) | ~(x & y & z & ...)
  // ~a | ~b | ~c | ... | ~x | ~y | ~z | ...

  for (const auto& v : box.variables()) {
    const auto condition = MakeSatIntervalVar(pn, v, box[v]);
    if (is_true(condition)) continue;
    else if (!is_conjunction(condition)) AddLiteral(!condition); // already predicate-converted.
    else for (const auto& lit : get_operands(condition)) AddLiteral(!lit);
  }
  // ==>
  for (const Formula& f : conflicting_conjunction) AddLiteral(!predicate_abstractor_.Convert(f));

  cadical->add(0);
}

void SatSolver::AddBox(PredicateNormalizer& pn, const Box& base_box) {
  for (const auto& v : base_box.variables()) {
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
  AddLearnedClause(pn, base_conflict, base_box);
  const auto [
    all_related_conflicts,
    match_statistics
    ] = pn.FindSimilar(base_conflict, timeout);
  if (all_related_conflicts.empty()) {
    DREAL_LOG_ERROR("Clause did not match with itself... Adding regularly.");
    return match_statistics;
  }

  for (const auto& [conflict_clause, subs] : all_related_conflicts) {
    const auto conflict_box = substitutions_map::apply_substitution(base_box, subs, true);

    if (DREAL_LOG_INFO_ENABLED) { // todo: better gate.
      std::set conflict_clause_set(conflict_clause.begin(), conflict_clause.end());
      audit(!make_conjunction_SKIP_CHECKS_KUNAL_HACK(std::move(conflict_clause_set)), conflict_box);
    }

    AddLearnedClause(pn, conflict_clause, conflict_box);
  }
  return match_statistics;
}

Formula SatSolver::MakeSatIntervalVar(PredicateNormalizer &pn, const Variable& var, const Box::Interval& intv) {
  // DREAL_ASSERT(var.get_type() != Variable::Type::BOOLEAN);
  if (var.get_type() == Variable::Type::BOOLEAN) {
    if (intv.lb() == 1) return Formula{var};
    if (intv.ub() == 0) return !Formula{var};
    DREAL_ASSERT(intv.lb() == 0 && intv.ub() == 1);
    return Formula::True();
  } // else {

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
        AddLearnedClause(pn, {ub_pred, !gt.second}, {/* empty— will overflow stack otherwise. */});
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
        AddLearnedClause(pn, {lt.second, !ub_pred}, {/* empty— will overflow stack otherwise. */});
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
        AddLearnedClause(pn, {gt.second, !lb_pred}, {/* empty— will overflow stack otherwise. */});
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
        AddLearnedClause(pn, {lb_pred, !lt.second}, {/* empty— will overflow stack otherwise. */});
      }
      var_lb_preds[intv.lb()] = lb_pred;
    }
  }

  return lb_pred && ub_pred;
}

bool SatSolver::learning(const int size) {
  buffer_i = 0;
  expected_clause_size = size;
  return size < BUFFER_SIZE;
}

void SatSolver::learn(const int new_lit) {
  if (new_lit != 0) {
    DREAL_ASSERT(buffer_i < expected_clause_size);
    buffer[buffer_i] = new_lit;
    buffer_i++;
    return;
  } // else {

  DREAL_ASSERT(buffer_i == expected_clause_size);

  set<Formula> neg_conjunction;
  for (int i = 0; i < expected_clause_size; i++) {
    const int lit = buffer[i];
    const bool lit_is_neg = lit < 0;

    const auto sym_var_it = to_sym_var_.find(lit_is_neg ? -lit : +lit);
    DREAL_ASSERT(sym_var_it != to_sym_var_.end());
    const auto theory_lit_it = predicate_abstractor_.var_to_formula_map().find(sym_var_it->second);
    const Formula theory_lit =
      theory_lit_it == predicate_abstractor_.var_to_formula_map().end()
        ? Formula{sym_var_it->second}
        : theory_lit_it->second;
    neg_conjunction.emplace(
      !(lit_is_neg ? !theory_lit : theory_lit)
    );
  }
  const Formula sat_clause = !make_conjunction(neg_conjunction);
  std::cout << "LEARNED: " << sat_clause << std::endl;
}

}  // namespace dreal
