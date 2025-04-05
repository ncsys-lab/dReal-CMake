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
        std::vector<std::pair<std::vector<Formula>, substitutions_map_ptr>> state{
                {{}, nullptr}
            }, next_state;
        DREAL_LOG_DEBUG("Finding matches for literals: {}", !make_conjunction(literals));

        for (const auto& form : literals) {
            next_state.clear();
            for (const auto& [some_literals, subs] : state) {
                for (const auto& [new_literal, new_subs] : find_matches(form, subs)) {
                    auto some_literals_copy = some_literals;
                    some_literals_copy.emplace_back(new_literal);
                    next_state.emplace_back(some_literals_copy, new_subs);
                }
            }
            state = std::move(next_state);
        }
        return state;
    }

    PatternMatchingTrie::f_matches_vec PatternMatchingTrie::find_matches(
        const Formula& f,
        const substitutions_map_ptr& substitutions
    ) const {
        DREAL_LOG_TRACE("Finding matches for formula {}", fmt::streamed(f));
        f_matches_vec matches;
        recMatchForm(f, f_root, substitutions, matches, PM_CONT_LAMBDA(n, s) {
            DREAL_LOG_ERROR("Unterminated partial matching? Not sure if this should ever be reachable.");
        });
        return matches;
    }

    PatternMatchingTrie::e_matches_vec PatternMatchingTrie::find_matches(
        const Expression& e,
        const substitutions_map_ptr& substitutions
    ) const {
        DREAL_LOG_TRACE("Finding matches for expression {}", fmt::streamed(e));
        e_matches_vec matches;
        recMatchExpr(e, e_root, substitutions, matches, PM_CONT_LAMBDA(n, s) {
            DREAL_LOG_ERROR("Unterminated partial matching? Not sure if this should ever be reachable.");
        });
        return matches;
    }

    void PatternMatchingTrie::insert(const Formula& f) {
        recAddForm(f, f_root, f);
    }

    void PatternMatchingTrie::insert(const Expression& e) {
        recAddExpr(e, e_root, e);
    }
}
