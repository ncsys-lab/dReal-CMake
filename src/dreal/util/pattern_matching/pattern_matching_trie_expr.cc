//
// Created by Kunal Sheth on 2/19/25.
//

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/symbolic/symbolic_expression_cell.h>
#include <dreal/util/assert.h>
#include <dreal/util/logging.h>

namespace dreal
{
#define VISIT_DECL(name) \
PatternMatchingTrie::e_partial_matches_vec PatternMatchingTrie::name ( \
    const Expression &_e, const ExprNode &parent, \
    const substitutions_map_ptr &substitutions, e_matches_vec &matches \
) const
#define ADD_DECL(name) \
PatternMatchingTrie::ExprNode& PatternMatchingTrie::name (const Expression &e, ExprNode &parent, const std::optional<Expression> &is_terminal)

    ADD_DECL(VisitVariable) {
        // todo: dedupe?
        return parent.c(ExpressionKind::Var).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitVariable) {
        const auto& e = get_variable(_e);
        e_partial_matches_vec partial_matches;
        for (auto& node : parent.c(ExpressionKind::Var)) {
            const auto& matched_e = get_variable(*node.leaf);
            const auto matched_subs = substitutions_map_node::attempt_substitution(substitutions, matched_e, e);

            if (!matched_subs.has_value())
                // type check failed
                // or match already substituted for something else, stop.
                continue;

            else if (!node.terminal_expression.has_value())
                // partial match, keep going!
                partial_matches.emplace_back(&node, *matched_subs);

            else if (substitutions_map_node::verify_substitutions(*matched_subs))
                // terminal match! BINGO!
                matches.emplace_back(*node.terminal_expression, *matched_subs);
        }
        return partial_matches;
    }

    ADD_DECL(VisitConstant) {
        return parent.c(ExpressionKind::Constant).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitConstant) {
        const auto e = get_constant_value(_e);
        e_partial_matches_vec partial_matches;
        for (auto& node : parent.c(ExpressionKind::Constant)) {
            const auto& matched_e = get_constant_value(*node.leaf);
            if (e != matched_e) continue;
            else if (!node.terminal_expression.has_value())
                partial_matches.emplace_back(&node, substitutions);
            else if (substitutions_map_node::verify_substitutions(substitutions)) {
                matches.emplace_back(*node.terminal_expression, substitutions);
            }
        }
        return partial_matches;
    }

    ADD_DECL(VisitRealConstant) {
        return parent.c(ExpressionKind::RealConstant).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitRealConstant) {
        const auto e = to_real_constant(_e);
        e_partial_matches_vec partial_matches;
        for (auto& node : parent.c(ExpressionKind::RealConstant)) {
            const auto& matched_e = to_real_constant(*node.leaf);
            if (!e->EqualTo(*matched_e)) continue; // confirmed non-recursive / simple one-liner
            else if (!node.terminal_expression.has_value())
                partial_matches.emplace_back(&node, substitutions);
            else if (substitutions_map_node::verify_substitutions(substitutions))
                matches.emplace_back(*node.terminal_expression, substitutions);
        }
        return partial_matches;
    }

    ADD_DECL(VisitAddition) {
        auto& n1 = parent.c(ExpressionKind::Add).emplace_back(e);
        ExprNode *stateCoeff = &n1, *stateExpr = nullptr;
        std::multimap<double, Expression> multimap; // canonicalize
        for (auto& [expr, coeff] : to_addition(e)->get_expr_to_coeff_map()) {
            multimap.emplace(coeff, expr);
        }
        size_t i = 0;
        for (const auto& [coeff, expr] : multimap) {
            stateExpr = &recAddExpr(coeff, *stateCoeff, {});
            stateCoeff = &recAddExpr(
                expr, *stateExpr,
                i++ == multimap.size() - 1 ? is_terminal : std::optional<Expression>{}
            );
        }
        return *stateCoeff;
    }

    VISIT_DECL(VisitAddition) {
        e_partial_matches_vec partial_matches;
        const auto e = to_addition(_e);
        for (auto& n1 : parent.c(ExpressionKind::Add)) {
            const auto& m = to_addition(*n1.leaf);
            if (e->get_constant() != m->get_constant()) continue;
            if (e->get_expr_to_coeff_map().size() != m->get_expr_to_coeff_map().size()) continue;

            std::multimap<double, Expression> e_canon_coeff_to_expr_map;
            for (auto& [expr, coeff] : e->get_expr_to_coeff_map())
                e_canon_coeff_to_expr_map.emplace(coeff, expr);
            e_partial_matches_vec state{{&n1, substitutions}}, next_state;
            for (auto& [coeff,expr] : e_canon_coeff_to_expr_map) {
                next_state.clear();
                for (auto& [n2,s2] : state) {
                    for (
                        auto& [n3,s3] :
                        recMatchExpr(coeff, *n2, s2, matches)
                    ) {
                        DREAL_ASSERT(!n1.terminal_expression.has_value());
                        DREAL_ASSERT(!n2->terminal_expression.has_value());
                        DREAL_ASSERT(!n3->terminal_expression.has_value());
                        auto n4s4 = recMatchExpr(expr, *n3, s3, matches);
                        next_state.insert(
                            next_state.end(),
                            std::make_move_iterator(n4s4.begin()), std::make_move_iterator(n4s4.end())
                        );
                    }
                }
                state = std::move(next_state);
            }
            partial_matches.insert(
                partial_matches.end(),
                std::make_move_iterator(state.begin()), std::make_move_iterator(state.end())
            );
        }
        return partial_matches;
    }

    ADD_DECL(VisitMultiplication) {
        auto& n1 = parent.c(ExpressionKind::Mul).emplace_back(e);
        ExprNode *stateExp = &n1, *stateBase = nullptr;
        std::multimap<Expression, Expression> multimap; // canonicalize..ish
        for (auto& [base, expo] : to_multiplication(e)->get_base_to_exponent_map()) {
            multimap.emplace(expo, base);
        }
        size_t i = 0;
        for (const auto& [expo, base] : multimap) {
            stateBase = &recAddExpr(expo, *stateExp, {});
            stateExp = &recAddExpr(
                base, *stateBase,
                i++ == multimap.size() - 1 ? is_terminal : std::optional<Expression>{}
            );
        }
        return *stateExp;
    }

    VISIT_DECL(VisitMultiplication) {
        e_partial_matches_vec partial_matches;
        const auto e = to_multiplication(_e);
        for (auto& n1 : parent.c(ExpressionKind::Mul)) {
            const auto& m = to_multiplication(*n1.leaf);
            if (e->get_constant() != m->get_constant()) continue;
            if (e->get_base_to_exponent_map().size() != m->get_base_to_exponent_map().size()) continue;

            std::multimap<Expression, Expression> e_canon_expo_to_base_map;
            for (auto& [base, expo] : e->get_base_to_exponent_map())
                e_canon_expo_to_base_map.emplace(expo, base);
            e_partial_matches_vec state{{&n1, substitutions}}, next_state;
            for (auto& [expo,base] : e_canon_expo_to_base_map) {
                next_state.clear();
                for (auto& [n2,s2] : state) {
                    for (
                        auto& [n3,s3] :
                        recMatchExpr(expo, *n2, s2, matches)
                    ) {
                        DREAL_ASSERT(!n1.terminal_expression.has_value());
                        DREAL_ASSERT(!n2->terminal_expression.has_value());
                        DREAL_ASSERT(!n3->terminal_expression.has_value());
                        auto n4s4 = recMatchExpr(base, *n3, s3, matches);
                        next_state.insert(
                            next_state.end(),
                            std::make_move_iterator(n4s4.begin()), std::make_move_iterator(n4s4.end())
                        );
                    }
                }
                state = std::move(next_state);
            }
            partial_matches.insert(
                partial_matches.end(),
                std::make_move_iterator(state.begin()), std::make_move_iterator(state.end())
            );
        }
        return partial_matches;
    }

    PatternMatchingTrie::ExprNode& PatternMatchingTrie::BinaryOpAddHelper(
        const Expression& e, const ExpressionKind& k,
        ExprNode& parent, const std::optional<Expression>& is_terminal
    ) {
        auto& n1 = parent.c_leafless(k);
        auto& n2 = recAddExpr(get_first_argument(e), n1, {});
        auto& n3 = recAddExpr(get_second_argument(e), n2, is_terminal);
        return n3;
    }

    PatternMatchingTrie::e_partial_matches_vec PatternMatchingTrie::BinaryOpMatchHelper(
        const Expression& _e, const ExpressionKind& k,
        const ExprNode& parent, const substitutions_map_ptr& s1, e_matches_vec& matches
    ) const {
        e_partial_matches_vec partial_matches;
        for (auto& n1 : parent.c(k)) {
            for (auto& [n2, s2] : recMatchExpr(get_first_argument(_e), n1, s1, matches)) {
                DREAL_ASSERT(!n1.leaf.has_value());
                DREAL_ASSERT(!n1.terminal_expression.has_value());
                DREAL_ASSERT(!n2->terminal_expression.has_value());
                auto n3s3 = recMatchExpr(get_second_argument(_e), *n2, s2, matches);
                partial_matches.insert(
                    partial_matches.end(),
                    std::make_move_iterator(n3s3.begin()), std::make_move_iterator(n3s3.end())
                );
            }
        }
        return partial_matches;
    }

    PatternMatchingTrie::ExprNode& PatternMatchingTrie::UnaryOpAddHelper(
        const Expression& e, const ExpressionKind& k,
        ExprNode& parent, const std::optional<Expression>& is_terminal
    ) {
        auto& n1 = parent.c_leafless(k);
        auto& n2 = recAddExpr(get_argument(e), n1, is_terminal);
        return n2;
    }

    PatternMatchingTrie::e_partial_matches_vec PatternMatchingTrie::UnaryOpMatchHelper(
        const Expression& e, const ExpressionKind& k,
        const ExprNode& parent, const substitutions_map_ptr& s1, e_matches_vec& matches
    ) const {
        e_partial_matches_vec partial_matches;
        for (auto& n1 : parent.c(k)) {
            DREAL_ASSERT(!n1.leaf.has_value());
            DREAL_ASSERT(!n1.terminal_expression.has_value());
            auto n2s2 = recMatchExpr(get_argument(e), n1, s1, matches);
            partial_matches.insert(
                partial_matches.end(),
                std::make_move_iterator(n2s2.begin()), std::make_move_iterator(n2s2.end())
            );
        }
        return partial_matches;
    }

    ADD_DECL(VisitDivision) {
        return BinaryOpAddHelper(e, ExpressionKind::Div, parent, is_terminal);
    }

    VISIT_DECL(VisitDivision) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Div, parent, substitutions, matches);
    }

    ADD_DECL(VisitLog) {
        return UnaryOpAddHelper(e, ExpressionKind::Log, parent, is_terminal);
    }

    VISIT_DECL(VisitLog) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Log, parent, substitutions, matches);
    }

    ADD_DECL(VisitAbs) {
        return UnaryOpAddHelper(e, ExpressionKind::Abs, parent, is_terminal);
    }

    VISIT_DECL(VisitAbs) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Abs, parent, substitutions, matches);
    }

    ADD_DECL(VisitExp) {
        return UnaryOpAddHelper(e, ExpressionKind::Exp, parent, is_terminal);
    }

    VISIT_DECL(VisitExp) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Exp, parent, substitutions, matches);
    }

    ADD_DECL(VisitSqrt) {
        return UnaryOpAddHelper(e, ExpressionKind::Sqrt, parent, is_terminal);
    }

    VISIT_DECL(VisitSqrt) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Sqrt, parent, substitutions, matches);
    }

    ADD_DECL(VisitPow) {
        return BinaryOpAddHelper(e, ExpressionKind::Pow, parent, is_terminal);
    }

    VISIT_DECL(VisitPow) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Pow, parent, substitutions, matches);
    }

    ADD_DECL(VisitSin) {
        return UnaryOpAddHelper(e, ExpressionKind::Sin, parent, is_terminal);
    }

    VISIT_DECL(VisitSin) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Sin, parent, substitutions, matches);
    }

    ADD_DECL(VisitCos) {
        return UnaryOpAddHelper(e, ExpressionKind::Cos, parent, is_terminal);
    }

    VISIT_DECL(VisitCos) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Cos, parent, substitutions, matches);
    }

    ADD_DECL(VisitTan) {
        return UnaryOpAddHelper(e, ExpressionKind::Tan, parent, is_terminal);
    }

    VISIT_DECL(VisitTan) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Tan, parent, substitutions, matches);
    }

    ADD_DECL(VisitAsin) {
        return UnaryOpAddHelper(e, ExpressionKind::Asin, parent, is_terminal);
    }

    VISIT_DECL(VisitAsin) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Asin, parent, substitutions, matches);
    }

    ADD_DECL(VisitAcos) {
        return UnaryOpAddHelper(e, ExpressionKind::Acos, parent, is_terminal);
    }

    VISIT_DECL(VisitAcos) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Acos, parent, substitutions, matches);
    }

    ADD_DECL(VisitAtan) {
        return UnaryOpAddHelper(e, ExpressionKind::Atan, parent, is_terminal);
    }

    VISIT_DECL(VisitAtan) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Atan, parent, substitutions, matches);
    }

    ADD_DECL(VisitAtan2) {
        return BinaryOpAddHelper(e, ExpressionKind::Atan2, parent, is_terminal);
    }

    VISIT_DECL(VisitAtan2) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Atan2, parent, substitutions, matches);
    }

    ADD_DECL(VisitSinh) {
        return UnaryOpAddHelper(e, ExpressionKind::Sinh, parent, is_terminal);
    }

    VISIT_DECL(VisitSinh) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Sinh, parent, substitutions, matches);
    }

    ADD_DECL(VisitCosh) {
        return UnaryOpAddHelper(e, ExpressionKind::Cosh, parent, is_terminal);
    }

    VISIT_DECL(VisitCosh) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Cosh, parent, substitutions, matches);
    }

    ADD_DECL(VisitTanh) {
        return UnaryOpAddHelper(e, ExpressionKind::Tanh, parent, is_terminal);
    }

    VISIT_DECL(VisitTanh) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Tanh, parent, substitutions, matches);
    }

    ADD_DECL(VisitMin) {
        return BinaryOpAddHelper(e, ExpressionKind::Min, parent, is_terminal);
    }

    VISIT_DECL(VisitMin) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Min, parent, substitutions, matches);
    }

    ADD_DECL(VisitMax) {
        return BinaryOpAddHelper(e, ExpressionKind::Max, parent, is_terminal);
    }

    VISIT_DECL(VisitMax) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Max, parent, substitutions, matches);
    }

    ADD_DECL(VisitIfThenElse) {
        const auto ite = to_if_then_else(e);
        auto& n1 = parent.c_leafless(ExpressionKind::IfThenElse);
        auto& n2 = recAddForm(ite->get_conditional_formula(), n1.init_switch_kind(), {});
        auto& n3 = recAddExpr(ite->get_then_expression(), n2.init_switch_kind(), {});
        auto& n4 = recAddExpr(ite->get_else_expression(), n3, is_terminal);
        return n4;
    }

    VISIT_DECL(VisitIfThenElse) {
        const auto e = to_if_then_else(_e);
        e_partial_matches_vec partial_matches;
        for (auto& n1 : parent.c(ExpressionKind::IfThenElse)) {
            DREAL_ASSERT(n1.switch_kind != nullptr);
            f_matches_vec f_matches;
            for (
                auto& [n2, s2] :
                recMatchForm(e->get_conditional_formula(), *n1.switch_kind, substitutions, f_matches)
            ) {
                DREAL_ASSERT(n2 != nullptr);
                DREAL_ASSERT(n2->switch_kind != nullptr);
                DREAL_ASSERT(f_matches.empty());
                for (
                    auto& [n3, s3] :
                    recMatchExpr(e->get_then_expression(), *n2->switch_kind, s2, matches)
                ) {
                    DREAL_ASSERT(!n1.leaf.has_value());
                    DREAL_ASSERT(!n1.terminal_expression.has_value());
                    DREAL_ASSERT(!n2->terminal_expression.has_value());
                    DREAL_ASSERT(!n3->terminal_expression.has_value());
                    auto n4s4 = recMatchExpr(e->get_else_expression(), *n3, s3, matches);
                    partial_matches.insert(
                        partial_matches.end(),
                        std::make_move_iterator(n4s4.begin()), std::make_move_iterator(n4s4.end())
                    );
                }
            }
        }
        return partial_matches;
    }

    ADD_DECL(VisitUninterpretedFunction) {
        return parent.c(ExpressionKind::UninterpretedFunction).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitUninterpretedFunction) {
        // todo:  can an uninterpreted function be substituted?  yeah probably...
        // but doesn't really make sense for dReal.
        const auto e = to_uninterpreted_function(_e);
        e_partial_matches_vec partial_matches;
        for (auto& node : parent.c(ExpressionKind::UninterpretedFunction)) {
            const auto& matched_e = to_uninterpreted_function(*node.leaf);
            if (!e->EqualTo(*matched_e)) continue; // confirmed non-recursive / simple one-liner
            else if (!node.terminal_expression.has_value())
                partial_matches.emplace_back(&node, substitutions);
            else if (substitutions_map_node::verify_substitutions(substitutions))
                matches.emplace_back(*node.terminal_expression, substitutions);
        }
        return partial_matches;
    }
#undef VISIT_DECL
#undef ADD_DECL
}
