//
// Created by Kunal Sheth on 2/19/25.
//

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/symbolic/symbolic_expression_cell.h>
#include "dreal/symbolic/odes/symbolic_odes_cell.h"
#include <dreal/util/assert.h>
#include <dreal/util/logging.h>

#include "dreal/util/exception.h"
#include "dreal/util/iterators.h"

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
#define ADD_DECL(name) \
PatternMatchingTrie::FormNode& PatternMatchingTrie::name (const Formula &f, FormNode &parent, const std::optional<Formula> &is_terminal)

    ADD_DECL(VisitFalse) {
        DREAL_ASSERT(parent.c_like(f).empty()); // todo? idk. idk how my own code works.
        return parent.c_like(f).emplace_back(f, is_terminal);
    }

    VISIT_DECL(VisitFalse) {
        DREAL_ASSERT(parent.c_like(_f).size() == 1); // todo? idk. idk how my own code works.
        for (const auto& node : parent.c_like(_f)) {
            if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }

    ADD_DECL(VisitTrue) {
        DREAL_ASSERT(parent.c_like(f).empty()); // todo? idk. idk how my own code works.
        return parent.c_like(f).emplace_back(f, is_terminal);
    }

    VISIT_DECL(VisitTrue) {
        DREAL_ASSERT(parent.c_like(_f).size() == 1); // todo? idk. idk how my own code works.
        for (const auto& node : parent.c_like(_f)) {
            if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }

    ADD_DECL(VisitVariable) {
        return parent.c_like(f).emplace_back(f, is_terminal);
    }

    VISIT_DECL(VisitVariable) {
        const auto& f = get_variable(_f);
        for (const auto& node : parent.c_like(_f)) {
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
        auto& n1 = parent.c_leafless(k, f.get_al_hash());
        auto& n2 = recAddExpr(get_lhs_expression(f), n1.init_switch_kind(), {});
        auto& n3 = recAddExpr(get_rhs_expression(f), n2, {});
        auto& n4 = n3.init_switch_kind();
        n4.terminal_expression = is_terminal; // todo: figure out `is_terminal` and matches for formulas
        return n4;
    }

    void PatternMatchingTrie::BinaryOpMatchHelper(
        const Formula& _f, const FormulaKind& k,
        const FormNode& parent, substitutions_map& s1,
        const f_matches_vec& matches,
        const f_partial_matches_vec& partial_matches,
        const f_misses_vec& misses
    ) const {
        e_matches_vec e_matches = PM_CONT_LAMBDA(m, s) { DREAL_UNREACHABLE(); };
        for (const auto& n1 : parent.c(k, _f.get_al_hash())) {
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
        auto& n1 = parent.c_leafless(k, f.get_al_hash());
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

    void PatternMatchingTrie::NaryOpMatchHelper(
        const Formula& f, const FormulaKind& k,
        const FormNode& parent, substitutions_map& s1,
        const f_matches_vec& matches,
        const f_partial_matches_vec& partial_matches,
        const f_misses_vec& misses
    ) const {
        const auto *const _f = to_nary(f);
        for (const auto& n1 : parent.c(k, f.get_al_hash())) {
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
    VISIT_DECL(VisitEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Eq, parent, substitutions, matches, partial_matches, misses);
    }

    // todo: Canonical-ize NEQ to NOT + EQ ?
    ADD_DECL(VisitNotEqualTo) { return BinaryOpAddHelper(f, FormulaKind::Neq, parent, is_terminal); }
    VISIT_DECL(VisitNotEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Neq, parent, substitutions, matches, partial_matches, misses);
    }

    // todo: Canonical-ize GT/LT to just one or the other ?
    ADD_DECL(VisitGreaterThan) { return BinaryOpAddHelper(f, FormulaKind::Gt, parent, is_terminal); }
    VISIT_DECL(VisitGreaterThan) {
        return BinaryOpMatchHelper(_f, FormulaKind::Gt, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitGreaterThanOrEqualTo) { return BinaryOpAddHelper(f, FormulaKind::Geq, parent, is_terminal); }

    VISIT_DECL(VisitGreaterThanOrEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Geq, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitLessThan) { return BinaryOpAddHelper(f, FormulaKind::Lt, parent, is_terminal); }
    VISIT_DECL(VisitLessThan) {
        return BinaryOpMatchHelper(_f, FormulaKind::Lt, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitLessThanOrEqualTo) { return BinaryOpAddHelper(f, FormulaKind::Leq, parent, is_terminal); }

    VISIT_DECL(VisitLessThanOrEqualTo) {
        return BinaryOpMatchHelper(_f, FormulaKind::Leq, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitConjunction) { return NaryOpAddHelper(f, FormulaKind::And, parent, is_terminal); }
    VISIT_DECL(VisitConjunction) {
        NaryOpMatchHelper(_f, FormulaKind::And, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitDisjunction) { return NaryOpAddHelper(f, FormulaKind::Or, parent, is_terminal); }
    VISIT_DECL(VisitDisjunction) {
        NaryOpMatchHelper(_f, FormulaKind::Or, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitNegation) {
        auto& n1 = parent.c_leafless(FormulaKind::Not, f.get_al_hash());
        return recAddForm(get_operand(f), n1, is_terminal);
    }

    VISIT_DECL(VisitNegation) {
        for (const auto& n1 : parent.c_like(_f)) {
            DREAL_ASSERT(!n1.leaf.has_value());
            DREAL_ASSERT(!n1.terminal_expression.has_value());
            recMatchForm(get_operand(_f), n1, substitutions, matches, partial_matches, misses);
        }
    }

    ADD_DECL(VisitForall) {
        // todo: temporary implementation...
        // I haven't put much thought into this.
        return parent.c_like(f).emplace_back(f, is_terminal);
    }

    VISIT_DECL(VisitForall) {
        // temporary implementation...
        // I haven't put much thought into this.
        const auto *const f = to_forall(_f);
        for (const auto& node : parent.c_like(_f)) {
            const auto matched_f = to_forall(*node.leaf);
            if (!f->EqualTo(*matched_f))
                misses(substitutions_map::CONST_MISS);
            else if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }

    ADD_DECL(VisitForallT) {
        const auto* const ft = to_forallT(f);
        auto& n1 = parent.c_like(f).emplace_back(f); // contains flow.
        auto& n2 = recAddExpr(ft->get_lb(), n1.init_switch_kind(), {});
        auto& n3 = recAddExpr(ft->get_ub(), n2, {});

        // PRIOR THINKING:
        // `ft->get_bound_f()` is bound, not free. Should be excluded from alpha-renaming.
        // auto& n4 = recAddForm(ft->get_bound_f(), n3.init_switch_kind(), is_terminal);

        // UPDATE:
        // okay... so bound is BOUND TO THE INTEGRAL `(= vecT (integ ... vec0...))` formula rather than the FLOW itself.
        // since vecT of the integral formula is free, bound's variables need to be renamed as well
        // n3.init_switch_kind().terminal_expression = is_terminal;
        // return *n3.switch_kind;

        auto& n4 = recAddForm(ft->get_bound_f(), n3.init_switch_kind(), is_terminal);
        return n4;
    }

    VISIT_DECL(VisitForallT) {
        const auto f = to_forallT(_f);
        e_matches_vec e_matches = PM_CONT_LAMBDA(m, s) { DREAL_UNREACHABLE(); };
        f_matches_vec f_matches = PM_CONT_LAMBDA(m, s) { DREAL_UNREACHABLE(); };
        for (const auto& n1 : parent.c_like(_f)) {
            const auto m = to_forallT(*n1.leaf);
            // if (!f->get_bound_f().EqualTo(m->get_bound_f())) continue; // see "UPDATE" in corresponding `ADD_DECL`
            if (*f->get_flow() != *m->get_flow()) continue;
            DREAL_ASSERT(n1.switch_kind != nullptr);
            recMatchExpr(f->get_lb(), *n1.switch_kind, substitutions, e_matches, PM_CONT_LAMBDA(n2, s2) {
                recMatchExpr(f->get_ub(), n2, s2, e_matches, PM_CONT_LAMBDA(n3, s3) {
                    DREAL_ASSERT(!n1.terminal_expression.has_value());
                    DREAL_ASSERT(!n2.terminal_expression.has_value());
                    DREAL_ASSERT(!n3.terminal_expression.has_value());
                    DREAL_ASSERT(n3.switch_kind != nullptr);
                    // see "UPDATE" in corresponding `ADD_DECL`
                    //      const auto& fn3 = *n3.switch_kind;
                    //      if (fn3.terminal_expression.has_value()) { matches(*fn3.terminal_expression, s3); }
                    //      else { partial_matches(fn3, s3); }
                    recMatchForm(f->get_bound_f(), *n3.switch_kind, s3, matches, partial_matches, misses);
                }, misses);
            }, misses);
        }
    }

    ADD_DECL(VisitIntegral) {
        const auto* const i = to_integral(f);
        auto& n1 = parent.c_like(f).emplace_back(f); // contains flow.
        auto& n2 = recAddExpr(i->get_time_0(), n1.init_switch_kind(), {});
        auto& n3 = recAddExpr(i->get_time_t(), n2, {});
        ExprNode* stateV = &n3;
        for (const auto& v : concat_view(i->get_vec_0(), i->get_vec_t())) {
            stateV = &recAddExpr(v, *stateV, {});
        }
        stateV->init_switch_kind().terminal_expression = is_terminal;
        return *stateV->switch_kind;
    }

    VISIT_DECL(VisitIntegral) {
        const auto f = to_integral(_f);
        e_matches_vec e_matches = PM_CONT_LAMBDA(m, s) { DREAL_UNREACHABLE(); };
        f_matches_vec f_matches = PM_CONT_LAMBDA(m, s) { DREAL_UNREACHABLE(); };
        for (const auto& n1 : parent.c_like(_f)) {
            const auto m = to_integral(*n1.leaf);
            if (*f->get_flow() != *m->get_flow()) continue;
            DREAL_ASSERT(n1.switch_kind != nullptr);
            recMatchExpr(f->get_time_0(), *n1.switch_kind, substitutions, e_matches, PM_CONT_LAMBDA(n2, s2) {
                recMatchExpr(f->get_time_t(), n2, s2, e_matches, PM_CONT_LAMBDA(n3, s3) {
                    DREAL_ASSERT(!n1.terminal_expression.has_value());
                    DREAL_ASSERT(!n2.terminal_expression.has_value());
                    DREAL_ASSERT(!n3.terminal_expression.has_value());

                    const concat_view vec0t(f->get_vec_0(), f->get_vec_t());
                    const auto ibegin = vec0t.begin();
                    const auto iend = vec0t.end();
                    e_partial_matches_vec done = PM_CONT_LAMBDA(n4, s4) {
                        const auto& fn4 = *n4.switch_kind;
                        if (fn4.terminal_expression.has_value()) matches(*fn4.terminal_expression, s4);
                        else partial_matches(fn4, s4);
                    };
                    std::function<e_partial_matches_vec(typeof(ibegin))> it_to_match_op = [&](const auto& it1) {
                        return [&, /*copy*/ it1](const auto& n, auto& s) {
                            if (it1 == iend) {
                                DREAL_ASSERT(f->get_vec_0().size() == 1);
                                done(n, s);
                            }
                            auto it2 = it1;
                            ++it2;
                            if (it2 == iend) done(n, s);
                            else recMatchExpr(*it2, n, s, e_matches, it_to_match_op(it2), misses);
                        };
                    };
                    recMatchExpr(*ibegin, n3, s3, e_matches, it_to_match_op(ibegin), misses);
                }, misses);
            }, misses);
        }
    }

#undef VISIT_DECL
#undef ADD_DECL
}
