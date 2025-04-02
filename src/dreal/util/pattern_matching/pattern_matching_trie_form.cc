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
PatternMatchingTrie::f_partial_matches_vec PatternMatchingTrie::name ( \
    const Formula &_f, const FormNode &parent, \
    const substitutions_map_ptr &substitutions, f_matches_vec &matches \
) const
#define ADD_DECL(name) \
PatternMatchingTrie::FormNode& PatternMatchingTrie::name (const Formula &f, FormNode &parent, const std::optional<Formula> &is_terminal)

    ADD_DECL(VisitFalse) {
        return parent.c(FormulaKind::False).emplace_back(f, is_terminal);
    }

    VISIT_DECL(VisitFalse) {
        f_partial_matches_vec partial_matches;
        for (auto& node : parent.c(FormulaKind::False)) {
            if (!node.terminal_expression.has_value())
                partial_matches.emplace_back(&node, substitutions);
            else
                matches.emplace_back(*node.terminal_expression, substitutions);
        }
        return partial_matches;
    }

    ADD_DECL(VisitTrue) {
        return parent.c(FormulaKind::True).emplace_back(f, is_terminal);
    }

    VISIT_DECL(VisitTrue) {
        f_partial_matches_vec partial_matches;
        for (auto& node : parent.c(FormulaKind::True)) {
            if (!node.terminal_expression.has_value())
                partial_matches.emplace_back(&node, substitutions);
            else
                matches.emplace_back(*node.terminal_expression, substitutions);
        }
        return partial_matches;
    }

    ADD_DECL(VisitVariable) {
        return parent.c(FormulaKind::Var).emplace_back(f, is_terminal);
    }

    VISIT_DECL(VisitVariable) {
        const auto& f = get_variable(_f);
        f_partial_matches_vec partial_matches;
        for (auto& node : parent.c(FormulaKind::Var)) {
            const auto& matched_f = get_variable(*node.leaf);
            const auto matched_subs = attempt_substitution(substitutions, matched_f, f);

            if (matched_subs == nullptr) {
                // match already substituted for something else, stop.
                DREAL_LOG_TRACE("Formula pattern matching attempt failed after {} substitutions.", substitutions->size);
                continue;
            }

            else if (!node.terminal_expression.has_value())
                // partial match, keep going!
                partial_matches.emplace_back(&node, matched_subs);

            else {
                // terminal match! BINGO!
                DREAL_LOG_DEBUG("Found match after {} substitutions.", substitutions->size);
                matches.emplace_back(*node.terminal_expression, matched_subs);
            }
        }
        return partial_matches;
    }

    PatternMatchingTrie::FormNode& PatternMatchingTrie::BinaryOpAddHelper(
        const Formula& f, const FormulaKind& k,
        FormNode& parent, const std::optional<Formula>& is_terminal
    ) {
        auto& n1 = parent.c_leafless(k);
        auto& n2 = recAddExpr(get_lhs_expression(f), n1.init_switch_kind(), {});
        auto& n3 = recAddExpr(get_rhs_expression(f), n2, {});
        auto& n4 = n3.init_switch_kind();
        n4.terminal_expression = is_terminal; // todo: figure out `is_terminal` and matches for formulas
        return n4;
    }

    PatternMatchingTrie::f_partial_matches_vec PatternMatchingTrie::BinaryOpMatchHelper(
        const Formula& _f, const FormulaKind& k,
        const FormNode& parent, const substitutions_map_ptr& s1, f_matches_vec& matches
    ) const {
        e_matches_vec e_matches;
        e_partial_matches_vec e_partial_matches;
        for (auto& n1 : parent.c(k)) {
            for (auto& [n2, s2] : recMatchExpr(get_lhs_expression(_f), *n1.switch_kind, s1, e_matches)) {
                DREAL_ASSERT(!n1.leaf.has_value());
                DREAL_ASSERT(!n1.terminal_expression.has_value());
                DREAL_ASSERT(!n2->terminal_expression.has_value());
                auto n3s3 = recMatchExpr(get_rhs_expression(_f), *n2, s2, e_matches);
                e_partial_matches.insert(
                    e_partial_matches.end(),
                    std::make_move_iterator(n3s3.begin()), std::make_move_iterator(n3s3.end())
                );
            }
        }
        f_partial_matches_vec f_partial_matches;
        f_partial_matches.reserve(e_partial_matches.size());
        for (const auto& [node,subs] : e_partial_matches) {
            if (!node->switch_kind->terminal_expression.has_value())
                f_partial_matches.emplace_back(node->switch_kind, subs);
            else
                matches.emplace_back(*node->switch_kind->terminal_expression, subs);
        }
        return f_partial_matches;
    }

    ADD_DECL(VisitEqualTo) {
        return BinaryOpAddHelper(f, FormulaKind::Eq, parent, is_terminal);
    }

    VISIT_DECL(VisitEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Eq, parent, substitutions, matches);
    }

    // todo: Canonical-ize NEQ to NOT + EQ ?
    ADD_DECL(VisitNotEqualTo) {
        return BinaryOpAddHelper(f, FormulaKind::Neq, parent, is_terminal);
    }

    VISIT_DECL(VisitNotEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Neq, parent, substitutions, matches);
    }

    // todo: Canonical-ize GT/LT to just one or the other ?
    ADD_DECL(VisitGreaterThan) {
        return BinaryOpAddHelper(f, FormulaKind::Gt, parent, is_terminal);
    }

    VISIT_DECL(VisitGreaterThan) {
        return BinaryOpMatchHelper(_f, FormulaKind::Gt, parent, substitutions, matches);
    }

    ADD_DECL(VisitGreaterThanOrEqualTo) {
        return BinaryOpAddHelper(f, FormulaKind::Geq, parent, is_terminal);
    }

    VISIT_DECL(VisitGreaterThanOrEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Geq, parent, substitutions, matches);
    }

    ADD_DECL(VisitLessThan) {
        return BinaryOpAddHelper(f, FormulaKind::Lt, parent, is_terminal);
    }

    VISIT_DECL(VisitLessThan) {
        return BinaryOpMatchHelper(_f, FormulaKind::Lt, parent, substitutions, matches);
    }

    ADD_DECL(VisitLessThanOrEqualTo) {
        return BinaryOpAddHelper(f, FormulaKind::Leq, parent, is_terminal);
    }

    VISIT_DECL(VisitLessThanOrEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Leq, parent, substitutions, matches);
    }

    ADD_DECL(VisitConjunction) {
        auto& n1 = parent.c_leafless(FormulaKind::And);
        FormNode* state = &n1;
        const auto ops = to_conjunction(f)->get_operands();
        size_t i = 0;
        for (const auto& form : ops)
            state = &recAddForm(
                form, *state,
                i++ == ops.size() - 1 ? is_terminal : std::optional<Formula>{}
            );
        return *state;
    }

    VISIT_DECL(VisitConjunction) {
        f_partial_matches_vec partial_matches;
        const auto f = to_conjunction(_f);
        for (auto& n1 : parent.c(FormulaKind::And)) {
            f_partial_matches_vec state{{&n1, substitutions}}, next_state;
            for (const auto& form : f->get_operands()) {
                next_state.clear();
                for (auto& [n2,s2] : state) {
                    DREAL_ASSERT(!n1.terminal_expression.has_value());
                    DREAL_ASSERT(!n2->terminal_expression.has_value());
                    auto n3s3 = recMatchForm(form, *n2, s2, matches);
                    next_state.insert(
                        next_state.end(),
                        std::make_move_iterator(n3s3.begin()), std::make_move_iterator(n3s3.end())
                    );
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

    ADD_DECL(VisitDisjunction) {
        auto& n1 = parent.c_leafless(FormulaKind::Or);
        FormNode* state = &n1;
        const auto ops = to_disjunction(f)->get_operands();
        size_t i = 0;
        for (const auto& form : ops)
            state = &recAddForm(
                form, *state,
                i++ == ops.size() - 1 ? is_terminal : std::optional<Formula>{}
            );
        return *state;
    }

    VISIT_DECL(VisitDisjunction) {
        f_partial_matches_vec partial_matches;
        const auto f = to_disjunction(_f);
        for (auto& n1 : parent.c(FormulaKind::Or)) {
            f_partial_matches_vec state{{&n1, substitutions}}, next_state;
            for (const auto& form : f->get_operands()) {
                next_state.clear();
                for (auto& [n2,s2] : state) {
                    DREAL_ASSERT(!n1.terminal_expression.has_value());
                    DREAL_ASSERT(!n2->terminal_expression.has_value());
                    auto n3s3 = recMatchForm(form, *n2, s2, matches);
                    next_state.insert(
                        next_state.end(),
                        std::make_move_iterator(n3s3.begin()), std::make_move_iterator(n3s3.end())
                    );
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

    ADD_DECL(VisitNegation) {
        auto& n1 = parent.c_leafless(FormulaKind::Not);
        return recAddForm(get_operand(f), n1, is_terminal);
    }

    VISIT_DECL(VisitNegation) {
        f_partial_matches_vec partial_matches;
        for (auto& n1 : parent.c(FormulaKind::Not)) {
            DREAL_ASSERT(!n1.leaf.has_value());
            DREAL_ASSERT(!n1.terminal_expression.has_value());
            auto n2s2 = recMatchForm(get_operand(_f), n1, substitutions, matches);
            partial_matches.insert(
                partial_matches.end(),
                std::make_move_iterator(n2s2.begin()), std::make_move_iterator(n2s2.end())
            );
        }
        return partial_matches;
    }

    ADD_DECL(VisitForall) {
        throw DREAL_RUNTIME_ERROR("Pattern matching of quantifiers is currently unsupported {}",f);
    }

    VISIT_DECL(VisitForall) {
        throw DREAL_RUNTIME_ERROR("Pattern matching of quantifiers is currently unsupported {}", _f);
    }
#undef VISIT_DECL
#undef ADD_DECL
}
