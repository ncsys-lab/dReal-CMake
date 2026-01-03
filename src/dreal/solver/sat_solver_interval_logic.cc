//
// Created by Kunal Sheth on 4/17/25.
//

#include <algorithm>
#include <float.h>
#include <dreal/version.h>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include "dreal/util/logging.h"

#include "auditor.h"
#include "dreal/util/pattern_matching/matching_stats_t.h"
#include "dreal/util/predicate_normalizer.h"
#include "filter_assertion.h"
#include "sat_solver.h"
#include "dreal/util/pattern_matching/CAV26_varname_parser.h"
#include "dreal/util/pattern_matching/substitutions_map.h"

namespace dreal
{
    void SatSolver::AddLearnedClauseDirect(
        const std::vector<Formula>& conflicting_conjunction, const Box& unfitted_box
    ) {
        sat_log_label_clause("SatSolver::AddLearnedClauseDirect");
        for (const Formula& f : conflicting_conjunction) {
            AddLiteral(!predicate_abstractor_.Convert(f));
        }
        cadical->add(0);
        sat_log_literal0();

        if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
            std::vector<Formula> a;
            for (const Formula& f : conflicting_conjunction) a.emplace_back(!predicate_abstractor_.Convert(f));
            theory_audit_literals("AddLearnedClauseDirect", predicate_abstractor_, a, unfitted_box);
        }
    }

    /*void FlattenIntervalClause(const Formula& intv, const std::function<void(const Formula&)>& f) {
        if (is_true(intv)) { /* no-op #1# }
        else if (is_conjunction(intv)) {
            const auto ops = get_operands(intv);
            DREAL_ASSERT(ops.size() == 2);
            for (const auto& op : ops) f(op);
        }
        else f(intv);
    }*/

    /*void SatSolver::AddLearnedClause(
        PredicateNormalizer& pn,
        const std::vector<Formula>& conflicting_conjunction, const Box& unfitted_box
    ) {
        sat_log_label_clause("SatSolver::AddLearnedClause - Boxes");
        for (const auto& lit : conflicting_conjunction) {
            if (lit.GetFreeVariables().size() != 1 || !is_relational(lit)) continue;
            const Variable& var = *lit.GetFreeVariables().begin();

            const Box infinite_box{{var}};
            Box fitted_box{{var}};
            fitted_box[var] = unfitted_box[var];
            const auto r = FilterAssertion(lit, &fitted_box);

            if (r.filtered) {
                // MakeSatIntervalVarWithClauses automatically builds implications
                // if !r.change:
                //      unfitted => lit
                // if  r.change:
                //      lit /\ unfitted => fitted

                MakeSatIntervalVarWithClauses(pn, var, infinite_box[var]);
                // MakeSatIntervalVarWithClauses(pn, var, unfitted_box[var]); // redundant, made later.
                MakeSatIntervalVarWithClauses(pn, var, fitted_box[var]);
            }

            /*else if (r.filtered && r.changed) {
                const auto unfitted = MakeSatIntervalVarWithClauses(pn, var, unfitted_box[var]);
                const auto fitted = MakeSatIntervalVarWithClauses(pn, var, fitted_box[var]);

                // lit /\ unfitted => fitted
                // ~(lit /\ unfitted) \/ fitted
                // ~lit \/ ~unfitted \/ fitted

                AddLiteral(!predicate_abstractor_.Convert(lit));
                FlattenIntervalClause(unfitted, [&](const auto& il) { AddLiteral(!il); });
                FlattenIntervalClause(fitted, [&](const auto& il) { AddLiteral(il); });
                cadical->add(0);
                sat_log_literal0();

                if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
                    std::vector<Formula> a;
                    a.emplace_back(!predicate_abstractor_.Convert(lit));
                    FlattenIntervalClause(unfitted, [&](const auto& il) { a.emplace_back(!il); });
                    FlattenIntervalClause(fitted, [&](const auto& il) { a.emplace_back(il); });
                    audit_literals(predicate_abstractor_, a, {});
                }
            }
            else if (!r.filtered && !r.changed) {
                // no-op, was mistaken for something else...
                // e.g. `((5 + 6 * x + 2 * pow(x, 2)) < 0)` could end up here
            }
            else
                DREAL_UNREACHABLE();#1#
        }

        // a & b & c & ... ==> ~(x & y & z & ...)
        // ~(a & b & c & ...) | ~(x & y & z & ...)
        // ~a | ~b | ~c | ... | ~x | ~y | ~z | ...
        std::vector<Formula> lhs_implies;
        lhs_implies.reserve(unfitted_box.size() * 2);
        for (const auto& v : unfitted_box.variables()) {
            const auto condition = MakeSatIntervalVarWithClauses(pn, v, unfitted_box[v]);
            // need to use lhs_implies since MakeSatIntervalVarWithClauses adds its own sequence of clauses
            // terminating with `cadical->add(0)`
            FlattenIntervalClause(condition, [&](const auto& l) { lhs_implies.emplace_back(!l); });
        }

        sat_log_label_clause("SatSolver::AddLearnedClause - Predicated Clause");
        for (const auto& lit : lhs_implies) AddLiteral(lit);
        // ==>
        for (const Formula& f : conflicting_conjunction) AddLiteral(!predicate_abstractor_.Convert(f));
        cadical->add(0);
        sat_log_literal0();

        if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
            theory_audit_formula("AddLearnedClause - Formula", !make_conjunction(conflicting_conjunction), unfitted_box);
            std::vector<Formula> a;
            for (const auto& lit : lhs_implies) a.emplace_back(lit);
            // ==>
            for (const Formula& f : conflicting_conjunction) a.emplace_back(!predicate_abstractor_.Convert(f));
            theory_audit_literals("AddLearnedClause - Literals", predicate_abstractor_, a, {});
        }
    }*/

    /*void SatSolver::AddBox(PredicateNormalizer& pn, const Box& base_box) {
        sat_log_label_clause("SatSolver::AddBox");
        for (const auto& v : base_box.variables()) {
            const auto condition = MakeSatIntervalVarWithClauses(pn, v, base_box[v]);
            FlattenIntervalClause(condition, [&](const auto& f) {
                AddLiteral(f);
                cadical->add(0);
                sat_log_literal0();
            });
        }
    }*/

#if CAV26_FILTER_SYMMETRIES
    std::pair<bool, bool> analyze_symmetries_npT_npL(const substitutions_map& subs) {
        bool npT = false;
        bool npL = false;
        std::optional<int> constant_time_offset{};
        for (const auto& [a,aP] : subs.get_map()) {
            if (npT && npL) break;

            const auto [pA,tA] = CAV26_VARNAME_PARSER(a.get_name());
            const auto [pAP,tAP] = CAV26_VARNAME_PARSER(aP.get_name());

            npT |= pA != pAP;

            if (tA.has_value() != tAP.has_value()) npT = true;
            if (tA.has_value() && tAP.has_value()) {
                const auto time_offset = *tAP - *tA;
                npT |= time_offset != constant_time_offset.value_or(time_offset);
                npL |= time_offset != 0;
                constant_time_offset = time_offset;
            }
        }
        return {npT, npL};
    }
#endif


    matching_stats_t SatSolver::AddLearnedClausePattern(
        PredicateNormalizer& pn,
        const std::vector<Formula>& base_conflict, const Box& base_box,
        const std::chrono::duration<uint64_t, std::micro> timeout
    ) {
        sat_log_label_clause("SatSolver::AddLearnedClausePattern");

        auto pnfs_result = pn.FindSimilar(base_conflict, base_box, DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED || CAV26_FILTER_SYMMETRIES, timeout);
        const auto& all_related_conflicts = pnfs_result.first;
        auto& match_statistics = pnfs_result.second;

        DREAL_ASSERT(match_statistics.matches == all_related_conflicts.size());
        if (all_related_conflicts.empty()) {
            DREAL_LOG_INFO("Clause did not match with itself... Adding regularly.");
            return match_statistics;
        }

        std::vector<bool> do_not_add_clause; //                            optional filtering,
        do_not_add_clause.resize(all_related_conflicts.size(), false); //  add all by default.

        std::vector<std::string_view> metadata_tags; // calculate metadata if needed, otherwise leave empty.

#if CAV26_FILTER_SYMMETRIES
        metadata_tags.resize(all_related_conflicts.size()); // empty string by default.
        DREAL_ASSERT(match_statistics.misses_bc.at(substitutions_map::CAV26_NOT_PURE_TIME) == 0);
        DREAL_ASSERT(match_statistics.misses_bc.at(substitutions_map::CAV26_NOT_PURE_LOGIC) == 0);
        DREAL_ASSERT(match_statistics.misses_bc.at(substitutions_map::CAV26_NOT_PURE_ANY) == 0);

        for (int i = 0; i < all_related_conflicts.size(); ++i) {
            const auto& [conflict_clause, subs] = all_related_conflicts[i];
            const auto [npT, npL] = analyze_symmetries_npT_npL(*subs);

            int stats_miss_reason = -1;
            if (npT && npL) {
                metadata_tags[i] = "np*";
                stats_miss_reason = substitutions_map::CAV26_NOT_PURE_ANY;
            }
            else if (npT) {
                metadata_tags[i] = "npT";
                stats_miss_reason = substitutions_map::CAV26_NOT_PURE_TIME;
            }
            else if (npL) {
                metadata_tags[i] = "npL";
                stats_miss_reason = substitutions_map::CAV26_NOT_PURE_LOGIC;
            }
            else {
                metadata_tags[i] = "P";
            }

            if ((CAV26_MATCH_PURE_TIME_SYM && npT) || (CAV26_MATCH_PURE_LOGIC_SYM && npL)) {
                ++match_statistics.misses_bc.at(stats_miss_reason);
                --match_statistics.matches;
                do_not_add_clause[i] = true;
            }
        }
#endif

        if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
            std::set s(base_conflict.begin(), base_conflict.end());
            theory_audit_formula(
                "AddLearnedClausePattern",
                !make_conjunction_SKIP_CHECKS_KUNAL_HACK(std::move(s)),
                base_box // conflict_box
            );
        }

        if (DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED) {
            pm_dump_all(base_conflict, all_related_conflicts, metadata_tags, do_not_add_clause);
        }

        DREAL_ASSERT(std::count(do_not_add_clause.begin(), do_not_add_clause.end(), false) == match_statistics.matches);
        for (int i = 0; i < all_related_conflicts.size(); ++i) {
            if (do_not_add_clause[i]) continue;
            const auto& [conflict_clause, subs] = all_related_conflicts[i];
            // const auto conflict_box = substitutions_map::apply_substitution(base_box, subs, true);
            // AddLearnedClause(pn, conflict_clause, conflict_box);
            AddLearnedClauseDirect(conflict_clause, base_box);
        }
        return match_statistics;
    }


    std::pair<Formula, bool> SatSolver::MakeSatUbVar(PredicateNormalizer& pn, const Variable& var, const double ub,
                                                     const bool inclusive) {
        if (!isfinite(ub)) return {Formula::True(), true};
        // else {

        // an iterator pointing to the first element that is greater or equal to ub
        auto& var_ub_preds = (inclusive ? all_incl_ub_predicates : all_excl_ub_predicates)[var];
        auto ub_gte_it = var_ub_preds.lower_bound(ub);
        if (ub_gte_it != var_ub_preds.end() && ub_gte_it->first == ub) {
            return {ub_gte_it->second, true};
        } // else {

        Formula ub_pred = inclusive ? var <= ub : var < ub;
        ub_pred = predicate_abstractor_.Convert(pn.Convert(ub_pred));
        MakeSatVar(get_variable(ub_pred));

        if (ub_gte_it != var_ub_preds.end()) {
            const auto& gt = *ub_gte_it;
            DREAL_ASSERT(ub < gt.first);
            DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", ub_pred, gt.second);
            // std::cout << ub_pred << " => " << gt.second << std::endl;
            // (x < Ub) => (x < Ub+ε)
            // = ~(x < Ub) \/ (x < Ub+ε)
            // = ~((x < Ub) /\ ~(x < Ub+ε))
            AddLearnedClauseDirect({ub_pred, !gt.second}, {});
        }
        if (ub_gte_it != var_ub_preds.begin()) {
            --ub_gte_it;
            const auto& lt = *ub_gte_it;
            DREAL_ASSERT(lt.first < ub);
            DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", lt.second, ub_pred);
            // std::cout << lt.second << " => " << ub_pred << std::endl;
            // (x < Ub-ε) => (x < Ub)
            // = ~(x < Ub-ε) \/ (x < Ub)
            // = ~((x < Ub-ε) /\ ~(x < Ub))
            AddLearnedClauseDirect({lt.second, !ub_pred}, {});
        }
        var_ub_preds[ub] = ub_pred;
        return {ub_pred, false};
    }


    std::pair<Formula, bool> SatSolver::MakeSatLbVar(PredicateNormalizer& pn, const Variable& var, const double lb,
                                                     const bool inclusive) {
        if (!isfinite(lb)) return {Formula::True(), true};
        // else {

        // an iterator pointing to the first element that is greater or equal to lb
        auto& var_lb_preds = (inclusive ? all_incl_lb_predicates : all_excl_lb_predicates)[var];
        auto lb_gte_it = var_lb_preds.lower_bound(lb);
        if (lb_gte_it != var_lb_preds.end() && lb_gte_it->first == lb) {
            return {lb_gte_it->second, true};
        } // else {

        Formula lb_pred = inclusive ? lb <= var : lb < var;
        lb_pred = predicate_abstractor_.Convert(pn.Convert(lb_pred));
        MakeSatVar(get_variable(lb_pred));

        if (lb_gte_it != var_lb_preds.end()) {
            const auto& gt = *lb_gte_it;
            DREAL_ASSERT(lb < gt.first);
            DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", gt.second, lb_pred);
            // std::cout << gt.second << " => " << lb_pred << std::endl;
            // (Lb+ε < x) => (Lb < x)
            // = ~(Lb+ε < x) \/ (Lb < x)
            // = ~((Lb+ε < x) /\ ~(Lb < x))
            AddLearnedClauseDirect({gt.second, !lb_pred}, {});
        }
        if (lb_gte_it != var_lb_preds.begin()) {
            --lb_gte_it;
            const auto& lt = *lb_gte_it;
            DREAL_ASSERT(lt.first < lb);
            DREAL_LOG_DEBUG("Adding SAT interval implication: {} => {}", lb_pred, lt.second);
            // std::cout << lb_pred << " => " << lt.second << std::endl;
            // (Lb < x) => (Lb-ε < x)
            // = ~(Lb < x) \/ (Lb-ε < x)
            // = ~((Lb < x) /\ ~(Lb-ε < x))
            AddLearnedClauseDirect({lb_pred, !lt.second}, {});
        }
        var_lb_preds[lb] = lb_pred;
        return {lb_pred, false};
    }

    /*Formula SatSolver::MakeSatIntervalVarWithClauses(PredicateNormalizer& pn, const Variable& var,
                                                     const Box::Interval& intv) {
        DREAL_LOG_DEBUG("SatSolver::MakeSatIntervalVarWithClauses({} ∈ {})",
                        fmt::streamed(var), fmt::streamed(intv));

        // DREAL_ASSERT(var.get_type() != Variable::Type::BOOLEAN);
        if (var.get_type() == Variable::Type::BOOLEAN) {
            if (intv.lb() == 1) return Formula{var};
            if (intv.ub() == 0) return !Formula{var};
            DREAL_ASSERT(intv.lb() == 0 && intv.ub() == 1);
            return Formula::True();
        } // else {

        const auto [lb, lb_cache_hit] = MakeSatLbVar(pn, var, intv.lb(), true);
        if (!lb_cache_hit) {
            DREAL_ASSERT(is_finite(intv.lb()));
            sat_log_label_clause("SatSolver::MakeSatIntervalVarWithClauses - LB");

            const auto [inv_lb, _] = MakeSatUbVar(
                pn, var, intv.lb(), false
            );
            // ~(lb & inv_lb)  &  ~(~lb & ~inv_lb)
            // ~lb \/ ~inv_lb  &  lb \/ inv_lb
            AddLiteral(!lb);
            AddLiteral(!inv_lb);
            cadical->add(0);
            sat_log_literal0();
            AddLiteral(lb);
            AddLiteral(inv_lb);
            cadical->add(0);
            sat_log_literal0();

            if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
                theory_audit_literals("MakeSatIntervalVarWithClauses - lb neg", predicate_abstractor_, {!lb, !inv_lb}, {});
                theory_audit_literals("MakeSatIntervalVarWithClauses - lb pos",predicate_abstractor_, {lb, inv_lb}, {});
            }
        }

        const auto [ub, ub_cache_hit] = MakeSatUbVar(pn, var, intv.ub(), true);
        if (!ub_cache_hit) {
            DREAL_ASSERT(is_finite(intv.ub()));
            sat_log_label_clause("SatSolver::MakeSatIntervalVarWithClauses - UB");

            const auto [inv_ub, _] = MakeSatLbVar(
                pn, var, intv.ub(), false
            );
            // ~(ub & inv_ub)  &  ~(~ub & ~inv_ub)
            // ~ub \/ ~inv_ub  &  ub \/ inv_ub
            AddLiteral(!ub);
            AddLiteral(!inv_ub);
            cadical->add(0);
            sat_log_literal0();
            AddLiteral(ub);
            AddLiteral(inv_ub);
            cadical->add(0);
            sat_log_literal0();

            if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
                theory_audit_literals("MakeSatIntervalVarWithClauses - ub neg", predicate_abstractor_, {!ub, !inv_ub}, {});
                theory_audit_literals("MakeSatIntervalVarWithClauses - ub pos", predicate_abstractor_, {ub, inv_ub}, {});
            }
        }

        return lb && ub;
    }*/

    bool SatSolver::learning(const int size) {
        buffer_i = 0;
        expected_clause_size = size;
        if (size <= LOG_INFO_SIZE) return true;
        if (DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED && size < MAX_BUFFER_SIZE) return true;
        return false;
    }

    void SatSolver::learn(const int new_lit) {
        if (new_lit != 0) {
            DREAL_ASSERT(buffer_i < expected_clause_size);
            buffer[buffer_i] = new_lit;
            buffer_i++;
            return;
        } // else {

        DREAL_ASSERT(buffer_i == expected_clause_size);

        sat_log_label_clause("SatSolver::learn");
        std::set<Formula> neg_conjunction;
        for (int i = 0; i < expected_clause_size; i++) {
            const int lit = buffer[i];
            sat_log_literal(lit);
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
        sat_log_literal0();
        if (DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) {
            const Formula sat_clause = !make_conjunction(neg_conjunction);
            sat_log_label_clause("i.e. " + sat_clause.to_string());
        }
        if (expected_clause_size <= LOG_INFO_SIZE) {
            const Formula sat_clause = !make_conjunction(neg_conjunction);
            std::cerr << "SAT Solver Learned: " << sat_clause << std::endl;
        }
    }
} // namespace dreal
