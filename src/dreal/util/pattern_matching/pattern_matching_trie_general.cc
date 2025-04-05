//
// Created by Kunal Sheth on 2/19/25.
//

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/util/assert.h>
#include <dreal/util/box.h>
#include <dreal/util/logging.h>

#include "pattern_matching_trie.h"

namespace dreal
{
    std::vector<std::pair<std::vector<Formula>, substitutions_map_ptr>>
    PatternMatchingTrie::find_matches(const std::set<Formula>& literals) const {
        DREAL_LOG_DEBUG("Finding matches for literals: {}", !make_conjunction(literals));

        std::vector<Formula> matches_vec;
        std::vector<std::pair<std::vector<Formula>, substitutions_map_ptr>> result;
        matches_vec.reserve(literals.size());

        const f_misses_vec misses = [](const auto& _) {};
        const f_partial_matches_vec partial_matches = PM_CONT_LAMBDA(n, s) {
            DREAL_LOG_ERROR("Unterminated partial matching?");
            DREAL_UNREACHABLE(); // everything should AT LEAST match itself !?!?!
        };
        const f_misses_vec all_literals_match = [&](const auto& s) {
            DREAL_ASSERT(matches_vec.size() == literals.size());
            // if (DREAL_LOG_DEBUG_ENABLED)
            for (const auto& lit : matches_vec) {
                DREAL_ASSERT(literals.count(
                    substitutions_map_node::apply_substitution(lit, s, false)
                ) == 1);
            }
            result.emplace_back(matches_vec, s);
        };

        const auto ibegin = literals.begin();
        const auto iend = literals.end();
        std::function<f_matches_vec(typeof(ibegin))> match_next_literal = [&](const auto& it1) {
            return [&, /*copy*/ it1](const auto& f, const auto& s2) {
                matches_vec.emplace_back(f);
                if (it1 == iend) {
                    DREAL_ASSERT(literals.size() == 1);
                    return all_literals_match(s2);
                }
                auto it2 = it1;
                ++it2;
                if (it2 == iend) all_literals_match(s2);
                else recMatchForm(*it2, f_root, s2, match_next_literal(it2), partial_matches, misses);
                matches_vec.pop_back();
            };
        };
        recMatchForm(*ibegin, f_root, {}, match_next_literal(ibegin), partial_matches, misses);

        return result;
    }

    std::vector<std::pair<Formula, substitutions_map_ptr>> PatternMatchingTrie::find_matches(
        const Formula& f,
        const substitutions_map_ptr& substitutions
    ) const {
        std::vector<std::pair<Formula, substitutions_map_ptr>> match_vec;
        DREAL_LOG_TRACE("Finding matches for formula {}", fmt::streamed(f));
        f_matches_vec matches;
        recMatchForm(
            f, f_root, substitutions,
            PM_CONT_LAMBDA(m, s) {
                match_vec.emplace_back(m, s);
            },
            PM_CONT_LAMBDA(n, s) {
                DREAL_LOG_ERROR("Unterminated partial matching? Not sure if this should ever be reachable.");
            },
            [](const auto& s) {}
        );
        return match_vec;
    }

    std::vector<std::pair<Expression, substitutions_map_ptr>> PatternMatchingTrie::find_matches(
        const Expression& e,
        const substitutions_map_ptr& substitutions
    ) const {
        DREAL_LOG_TRACE("Finding matches for expression {}", fmt::streamed(e));
        std::vector<std::pair<Expression, substitutions_map_ptr>> match_vec;
        recMatchExpr(
            e, e_root, substitutions,
            PM_CONT_LAMBDA(m, s) {
                match_vec.emplace_back(m, s);
            },
            PM_CONT_LAMBDA(n, s) {
                DREAL_LOG_ERROR("Unterminated partial matching? Not sure if this should ever be reachable.");
            },
            [](const auto& s) {}
        );
        return match_vec;
    }

    void PatternMatchingTrie::insert(const Formula& f) {
        recAddForm(f, f_root, f);
    }

    void PatternMatchingTrie::insert(const Expression& e) {
        recAddExpr(e, e_root, e);
    }
}
