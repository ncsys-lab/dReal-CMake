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
#include "dreal/util/predicate_heuristic.h"

#include <dreal/symbolic/symbolic_expression_cell.h>
#include <dreal/symbolic/symbolic_formula_cell.h>

#include "exception.h"
#include "logging.h"

namespace dreal
{
    PredicateHeuristic::predicate_stats_t PredicateHeuristic::collect_statistics(const Expression& e) {
        const auto it = ecache.find(e);
        if (it == ecache.cend()) {
            predicate_stats_t stats = {0};
            recHeurExpr(e, stats);
            stats.unique_variables = e.GetVariables().size();
            ecache.try_emplace(e, stats);
            return stats;
        }
        else return it->second;
    }

    PredicateHeuristic::predicate_stats_t PredicateHeuristic::collect_statistics(const Formula& f, const bool inverted) {
        auto& fcache = inverted ? fcache_neg : fcache_pos;
        const auto it = fcache.find(f);
        if (it == fcache.cend()) {
            predicate_stats_t stats = {0};
            recHeurForm(f, stats, inverted);
            stats.unique_variables = f.GetFreeVariables().size();
            if (
                !(is_negation(f) && is_nary(get_operand(f))) &&
                !is_nary(f) // these will be cached at a lower level
            ) fcache.try_emplace(f, stats);
            return stats;
        }
        else return it->second;
    }

#define ADD_DECL(name) void PredicateHeuristic::name (const Expression &e, predicate_stats_t &stats) const
    ADD_DECL(VisitVariable) {
        stats.variable_cntr++;
    }

    ADD_DECL(VisitConstant) {
        stats.constant_cntr++;
    }

    ADD_DECL(VisitRealConstant) {
        stats.realconstant_cntr++;
    }

    ADD_DECL(VisitAddition) {
        const auto& map = get_expr_to_coeff_map_in_addition(e);
        stats.addition_cntr += map.size();
        for (const auto& [expr, coeff] : map) {
            recHeurExpr(expr, stats);
        }
    }

    ADD_DECL(VisitMultiplication) {
        const auto map = get_base_to_exponent_map_in_multiplication(e);
        stats.multiplication_cntr += map.size();
        for (const auto& [base, exp] : map) {
            recHeurExpr(base, stats);
            recHeurExpr(exp, stats);
        }
    }

    ADD_DECL(VisitDivision) {
        stats.division_cntr++;
        BinaryOpHelper(e, stats);
    }

    ADD_DECL(VisitLog) {
        stats.log_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitAbs) {
        stats.abs_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitExp) {
        stats.exp_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitSqrt) {
        stats.sqrt_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitPow) {
        stats.pow_cntr++;
        BinaryOpHelper(e, stats);
    }

    ADD_DECL(VisitSin) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitCos) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitTan) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitAsin) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitAcos) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitAtan) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitAtan2) {
        stats.trigonometry_cntr++;
        BinaryOpHelper(e, stats);
    }

    ADD_DECL(VisitSinh) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitCosh) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitTanh) {
        stats.trigonometry_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitMin) {
        stats.min_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitMax) {
        stats.max_cntr++;
        UnaryOpHelper(e, stats);
    }

    ADD_DECL(VisitIfThenElse) {
        stats.ifthenelse_cntr++;
        const auto ite = to_if_then_else(e);
        recHeurForm(ite->get_conditional_formula(), stats, false);
        recHeurExpr(ite->get_then_expression(), stats);
        recHeurExpr(ite->get_else_expression(), stats);
    }

    ADD_DECL(VisitUninterpretedFunction) {
        stats.uninterpretedfunction_cntr++;
    }

#undef ADD_DECL
#define ADD_DECL(name) void PredicateHeuristic::name (const Formula &f, predicate_stats_t &stats, const bool &inverted) const
    ADD_DECL(VisitFalse) {
        stats.false_cntr++;
    }

    ADD_DECL(VisitTrue) {
        stats.true_cntr++;
    }

    ADD_DECL(VisitVariable) {
        stats.variable_cntr++;
    }

    ADD_DECL(VisitEqualTo) {
        stats.equalto_cntr++;
        BinaryOpHelper(f, stats, inverted);
    }

    ADD_DECL(VisitNotEqualTo) {
        stats.notequalto_cntr++;
        BinaryOpHelper(f, stats, inverted);
    }

    ADD_DECL(VisitGreaterThan) {
        stats.inequality_cntr++;
        BinaryOpHelper(f, stats, inverted);
    }

    ADD_DECL(VisitGreaterThanOrEqualTo) {
        stats.inequality_cntr++;
        BinaryOpHelper(f, stats, inverted);
    }

    ADD_DECL(VisitLessThan) {
        stats.inequality_cntr++;
        BinaryOpHelper(f, stats, inverted);
    }

    ADD_DECL(VisitLessThanOrEqualTo) {
        stats.inequality_cntr++;
        BinaryOpHelper(f, stats, inverted);
    }

    ADD_DECL(VisitConjunction) {
        const auto& ops = get_operands(f);
        stats.conjunction_cntr += ops.size();
        for (const auto& op : ops) recHeurFormCheckCache(op, stats, inverted);
    }

    ADD_DECL(VisitDisjunction) {
        const auto& ops = get_operands(f);
        stats.disjunction_cntr += ops.size();
        for (const auto& op : ops) recHeurFormCheckCache(op, stats, inverted);
    }

    ADD_DECL(VisitNegation) {
        stats.negation_cntr++;
        UnaryOpHelper(f, stats, !inverted);
    }

    ADD_DECL(VisitForall) {
        stats.forall_cntr++;
        recHeurForm(get_quantified_formula(f), stats, inverted);
        for (const auto & v : get_quantified_variables(f))
            recHeurExpr({v}, stats);
    }

#undef ADD_DECL

    void PredicateHeuristic::recHeurExprCheckCache(const Expression& e, predicate_stats_t& outer_stats) const {
        const auto it = ecache.find(e);
        if (it == ecache.cend()) recHeurExpr(e, outer_stats);
        else outer_stats += it->second;
    }

    void PredicateHeuristic::recHeurExpr(const Expression& e, predicate_stats_t& stats) const {
        return VisitExpression<void>(this, e, stats);
    }

    void PredicateHeuristic::recHeurFormCheckCache(const Formula& f, predicate_stats_t& outer_stats,
                                                const bool& inverted) const {
        auto& fcache = inverted ? fcache_neg : fcache_pos;
        const auto it = fcache.find(f);
        if (it == fcache.cend()) recHeurForm(f, outer_stats, inverted);
        else outer_stats += it->second;
    }

    void PredicateHeuristic::recHeurForm(const Formula& f, predicate_stats_t& stats, const bool& inverted) const {
        return VisitFormula<void>(this, f, stats, inverted);
    }

    void PredicateHeuristic::UnaryOpHelper(const Expression& e, predicate_stats_t& stats) const {
        return recHeurExpr(get_argument(e), stats);
    }

    void PredicateHeuristic::UnaryOpHelper(const Formula& f, predicate_stats_t& stats, const bool& inverted) const {
        return recHeurForm(get_operand(f), stats, inverted);
    }

    void PredicateHeuristic::BinaryOpHelper(const Expression& e, predicate_stats_t& stats) const {
        const auto b = to_binary(e);
        recHeurExpr(b->get_first_argument(), stats);
        recHeurExpr(b->get_second_argument(), stats);
    }

    void PredicateHeuristic::BinaryOpHelper(const Formula& f, predicate_stats_t& stats, const bool& inverted) const {
        const auto b = to_relational(f);
        recHeurExpr(b->get_lhs_expression(), stats);
        recHeurExpr(b->get_rhs_expression(), stats);
    }
} // namespace dreal
