//
// Created by Kunal Sheth on 2/19/25.
//

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/util/assert.h>
#include <dreal/util/box.h>

#include "pattern_matching_trie.h"

namespace dreal
{
    std::vector<std::pair<std::set<Formula>, substitutions_map_ptr>>
    PatternMatchingTrie::find_matches(const std::set<Formula>& literals) const {
        std::vector<std::pair<std::set<Formula>, substitutions_map_ptr>> state{
                {{}, nullptr}
            }, next_state;

        for (const auto& form : literals) {
            next_state.clear();
            for (const auto& [some_literals, subs] : state) {
                for (const auto& [new_literal, new_subs] : find_matches(form, subs)) {
                    // todo: check how bad this is.
                    // todo: check if using vector, and then converting to set, makes it faster. or just doing it all with set.
                    auto some_literals_copy = some_literals;
                    some_literals_copy.emplace(new_literal);
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
        f_matches_vec matches;
        recMatchForm(f, f_root, substitutions, matches);
        return matches;
    }

    PatternMatchingTrie::e_matches_vec PatternMatchingTrie::find_matches(
        const Expression& e,
        const substitutions_map_ptr& substitutions
    ) const {
        e_matches_vec matches;
        recMatchExpr(e, e_root, substitutions, matches);
        return matches;
    }

    void PatternMatchingTrie::insert(const Formula& f) {
        recAddForm(f, f_root, f);
    }

    void PatternMatchingTrie::insert(const Expression& e) {
        recAddExpr(e, e_root, e);
    }
}
