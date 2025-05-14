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
void PatternMatchingTrie::name ( \
    const Formula &_f, const FormNode &parent, \
    substitutions_map &substitutions, \
    const f_matches_vec &matches, \
    const f_partial_matches_vec& partial_matches, \
    const f_misses_vec& misses \
) const
#define EST_DECL(name) \
void PatternMatchingTrie::name (const Formula &_f, const FormNode &parent, uint64_t &branches, const f_est_continuation_vec& partial_matches) const
#define ADD_DECL(name) \
PatternMatchingTrie::FormNode& PatternMatchingTrie::name (const Formula &f, FormNode &parent, const std::optional<Formula> &is_terminal)

    ADD_DECL(VisitFalse) {
        DREAL_ASSERT(parent.c(FormulaKind::False).empty()); // todo? idk. idk how my own code works.
        return parent.c(FormulaKind::False).emplace_back(f, is_terminal);
    }

    EST_DECL(VisitFalse) {
        branches += parent.c(FormulaKind::False).size();
        DREAL_ASSERT(parent.c(FormulaKind::False).size() == 1); // todo? idk. idk how my own code works.
        for (auto& node : parent.c(FormulaKind::False)) {
            if (!node.terminal_expression.has_value())
                partial_matches(node);
        }
    }

    VISIT_DECL(VisitFalse) {
        DREAL_ASSERT(parent.c(FormulaKind::False).size() == 1); // todo? idk. idk how my own code works.
        for (auto& node : parent.c(FormulaKind::False)) {
            if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }

    ADD_DECL(VisitTrue) {
        DREAL_ASSERT(parent.c(FormulaKind::True).empty()); // todo? idk. idk how my own code works.
        return parent.c(FormulaKind::True).emplace_back(f, is_terminal);
    }

    EST_DECL(VisitTrue) {
        branches += parent.c(FormulaKind::True).size();
        DREAL_ASSERT(parent.c(FormulaKind::True).size() == 1); // todo? idk. idk how my own code works.
        for (auto& node : parent.c(FormulaKind::True)) {
            if (!node.terminal_expression.has_value())
                partial_matches(node);
        }
    }

    VISIT_DECL(VisitTrue) {
        DREAL_ASSERT(parent.c(FormulaKind::True).size() == 1); // todo? idk. idk how my own code works.
        for (auto& node : parent.c(FormulaKind::True)) {
            if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }

    ADD_DECL(VisitVariable) {
        return parent.c(FormulaKind::Var).emplace_back(f, is_terminal);
    }

    EST_DECL(VisitVariable) {
        const auto& f = get_variable(_f);
        for (auto& node : parent.c(FormulaKind::Var)) {
            const auto& matched_f = get_variable(*node.leaf);
            if (!matched_f.equal_to(f) /* confirmed non-recursive, simple one-liner */) {}
            else if (!node.terminal_expression.has_value()) {
                // partial match, keep going!
                partial_matches(node);
            }
        }
        branches *= parent.c(FormulaKind::Var).size();
    }

    VISIT_DECL(VisitVariable) {
        const auto& f = get_variable(_f);
        for (auto& node : parent.c(FormulaKind::Var)) {
            substitutions.push();

            const auto& matched_f = get_variable(*node.leaf);
            if (
                substitutions_map::substitution_status reason;
                (reason = substitutions.attempt_substitution(matched_f, f))
                != substitutions_map::SUCCESS
            )
                // type check failed
                // or match already substituted for something else, stop.
                misses(reason);

            else if (!node.terminal_expression.has_value())
                // partial match, keep going!
                partial_matches(node, substitutions);

            else
                // terminal match! BINGO!
                matches(*node.terminal_expression, substitutions);

            substitutions.pop();
        }
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

    void PatternMatchingTrie::BinaryOpEstHelper(
        const Formula& f, const FormulaKind& k,
        const FormNode& parent, uint64_t& branches, const f_est_continuation_vec& partial_matches
    ) const {
        branches += parent.c(k).size();
        for (auto& n1 : parent.c(k)) {
            recEstExpr(get_lhs_expression(f), *n1.switch_kind, branches, EST_CONT_LAMBDA(n2) {
                DREAL_ASSERT(!n1.leaf.has_value());
                DREAL_ASSERT(!n1.terminal_expression.has_value());
                DREAL_ASSERT(!n2.terminal_expression.has_value());
                recEstExpr(get_rhs_expression(f), n2, branches, EST_CONT_LAMBDA(n3) {
                    if (!n3.switch_kind->terminal_expression.has_value())
                        partial_matches(*n3.switch_kind);
                });
            });
        }
    }

    void PatternMatchingTrie::BinaryOpMatchHelper(
        const Formula& _f, const FormulaKind& k,
        const FormNode& parent, substitutions_map& s1,
        const f_matches_vec& matches,
        const f_partial_matches_vec& partial_matches,
        const f_misses_vec& misses
    ) const {
        e_matches_vec e_matches = PM_CONT_LAMBDA(m, s) { DREAL_UNREACHABLE(); };
        for (auto& n1 : parent.c(k)) {
            recMatchExpr(get_lhs_expression(_f), *n1.switch_kind, s1, e_matches, PM_CONT_LAMBDA(n2, s2) {
                DREAL_ASSERT(!n1.leaf.has_value());
                DREAL_ASSERT(!n1.terminal_expression.has_value());
                DREAL_ASSERT(!n2.terminal_expression.has_value());
                recMatchExpr(get_rhs_expression(_f), n2, s2, e_matches, PM_CONT_LAMBDA(n3, s3) {
                    if (!n3.switch_kind->terminal_expression.has_value())
                        partial_matches(*n3.switch_kind, s3);
                    else
                        matches(*n3.switch_kind->terminal_expression, s3);
                }, misses);
            }, misses);
        }
    }

    PatternMatchingTrie::FormNode& PatternMatchingTrie::NaryOpAddHelper(
        const Formula& f, const FormulaKind& k,
        FormNode& parent, const std::optional<Formula>& is_terminal
    ) {
        auto& n1 = parent.c_leafless(k);
        FormNode* state = &n1;
        const auto ops = get_operands(f);
        size_t i = 0;
        for (const auto& form : ops)
            state = &recAddForm(
                form, *state,
                i++ == ops.size() - 1 ? is_terminal : std::optional<Formula>{}
            );
        return *state;
    }

    void PatternMatchingTrie::NaryOpEstHelper(
        const Formula& f, const FormulaKind& k,
        const FormNode& parent, uint64_t& branches, const f_est_continuation_vec& partial_matches
    ) const {
        branches += parent.c(k).size();
        const auto _f = to_nary(f);
        for (auto& n1 : parent.c(k)) {
            DREAL_ASSERT(!n1.terminal_expression.has_value());
            const auto ibegin = _f->get_operands().begin();
            const auto iend = _f->get_operands().end();
            std::function<f_est_continuation_vec(typeof(ibegin))> it_to_match_op = [&](const auto& it1) {
                return [&, /*copy*/ it1](const auto& n2) {
                    if (it1 == iend) {
                        DREAL_ASSERT(get_operands(f).size() == 1);
                        return partial_matches(n2);
                    }
                    auto it2 = it1;
                    ++it2;
                    if (it2 == iend) partial_matches(n2);
                    else recEstForm(*it2, n2, branches, it_to_match_op(it2));
                };
            };
            recEstForm(*ibegin, n1, branches, it_to_match_op(ibegin));
        }
    }

    void PatternMatchingTrie::NaryOpMatchHelper(
        const Formula& f, const FormulaKind& k,
        const FormNode& parent, substitutions_map& s1,
        const f_matches_vec& matches,
        const f_partial_matches_vec& partial_matches,
        const f_misses_vec& misses
    ) const {
        const auto _f = to_nary(f);
        for (auto& n1 : parent.c(k)) {
            DREAL_ASSERT(!n1.terminal_expression.has_value());
            const auto ibegin = _f->get_operands().begin();
            const auto iend = _f->get_operands().end();
            std::function<f_partial_matches_vec(typeof(ibegin))> it_to_match_op = [&](const auto& it1) {
                return [&, /*copy*/ it1](const auto& n2, auto& s2) {
                    if (it1 == iend) {
                        DREAL_ASSERT(get_operands(f).size() == 1);
                        return partial_matches(n2, s2);
                    }
                    auto it2 = it1;
                    ++it2;
                    if (it2 == iend) partial_matches(n2, s2);
                    else recMatchForm(*it2, n2, s2, matches, it_to_match_op(it2), misses);
                };
            };
            recMatchForm(*ibegin, n1, s1, matches, it_to_match_op(ibegin), misses);
        }
    }

    ADD_DECL(VisitEqualTo) { return BinaryOpAddHelper(f, FormulaKind::Eq, parent, is_terminal); }
    EST_DECL(VisitEqualTo) { return BinaryOpEstHelper(_f, FormulaKind::Eq, parent, branches, partial_matches); }
    VISIT_DECL(VisitEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Eq, parent, substitutions, matches, partial_matches, misses);
    }

    // todo: Canonical-ize NEQ to NOT + EQ ?
    ADD_DECL(VisitNotEqualTo) { return BinaryOpAddHelper(f, FormulaKind::Neq, parent, is_terminal); }
    EST_DECL(VisitNotEqualTo) { return BinaryOpEstHelper(_f, FormulaKind::Neq, parent, branches, partial_matches); }
    VISIT_DECL(VisitNotEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Neq, parent, substitutions, matches, partial_matches, misses);
    }

    // todo: Canonical-ize GT/LT to just one or the other ?
    ADD_DECL(VisitGreaterThan) { return BinaryOpAddHelper(f, FormulaKind::Gt, parent, is_terminal); }
    EST_DECL(VisitGreaterThan) { return BinaryOpEstHelper(_f, FormulaKind::Gt, parent, branches, partial_matches); }
    VISIT_DECL(VisitGreaterThan) {
        return BinaryOpMatchHelper(_f, FormulaKind::Gt, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitGreaterThanOrEqualTo) { return BinaryOpAddHelper(f, FormulaKind::Geq, parent, is_terminal); }
    EST_DECL(VisitGreaterThanOrEqualTo) {
        return BinaryOpEstHelper(_f, FormulaKind::Geq, parent, branches, partial_matches);
    }

    VISIT_DECL(VisitGreaterThanOrEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Geq, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitLessThan) { return BinaryOpAddHelper(f, FormulaKind::Lt, parent, is_terminal); }
    EST_DECL(VisitLessThan) { return BinaryOpEstHelper(_f, FormulaKind::Lt, parent, branches, partial_matches); }
    VISIT_DECL(VisitLessThan) {
        return BinaryOpMatchHelper(_f, FormulaKind::Lt, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitLessThanOrEqualTo) { return BinaryOpAddHelper(f, FormulaKind::Leq, parent, is_terminal); }
    EST_DECL(VisitLessThanOrEqualTo) {
        return BinaryOpEstHelper(_f, FormulaKind::Leq, parent, branches, partial_matches);
    }

    VISIT_DECL(VisitLessThanOrEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Leq, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitConjunction) { return NaryOpAddHelper(f, FormulaKind::And, parent, is_terminal); }
    EST_DECL(VisitConjunction) { return NaryOpEstHelper(_f, FormulaKind::And, parent, branches, partial_matches); }
    VISIT_DECL(VisitConjunction) {
        NaryOpMatchHelper(_f, FormulaKind::And, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitDisjunction) { return NaryOpAddHelper(f, FormulaKind::Or, parent, is_terminal); }
    EST_DECL(VisitDisjunction) { return NaryOpEstHelper(_f, FormulaKind::Or, parent, branches, partial_matches); }
    VISIT_DECL(VisitDisjunction) {
        NaryOpMatchHelper(_f, FormulaKind::Or, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitNegation) {
        auto& n1 = parent.c_leafless(FormulaKind::Not);
        return recAddForm(get_operand(f), n1, is_terminal);
    }

    EST_DECL(VisitNegation) {
        branches += parent.c(FormulaKind::Not).size();
        auto& n1 = parent.c_leafless(FormulaKind::Not);
        recEstForm(get_operand(_f), n1, branches, partial_matches);
    }

    VISIT_DECL(VisitNegation) {
        for (auto& n1 : parent.c(FormulaKind::Not)) {
            DREAL_ASSERT(!n1.leaf.has_value());
            DREAL_ASSERT(!n1.terminal_expression.has_value());
            recMatchForm(get_operand(_f), n1, substitutions, matches, partial_matches, misses);
        }
    }

    ADD_DECL(VisitForall) {
        // temporary implementation...
        // I haven't put much thought into this.
        return parent.c(FormulaKind::Forall).emplace_back(f, is_terminal);
    }

    EST_DECL(VisitForall) {
        branches += parent.c(FormulaKind::Forall).size();
        const auto f = to_forall(_f);
        for (auto& node : parent.c(FormulaKind::Forall)) {
            const auto& matched_f = to_forall(*node.leaf);
            if (!f->EqualTo(*matched_f)) {} // recursive but whatever..
            else if (!node.terminal_expression.has_value())
                partial_matches(node);
        }
    }

    VISIT_DECL(VisitForall) {
        // temporary implementation...
        // I haven't put much thought into this.
        const auto f = to_forall(_f);
        for (auto& node : parent.c(FormulaKind::Forall)) {
            const auto& matched_f = to_forall(*node.leaf);
            if (!f->EqualTo(*matched_f))
                misses(substitutions_map::CONST_MISS);
            else if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }
#undef VISIT_DECL
#undef ADD_DECL
}
