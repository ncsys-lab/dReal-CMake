//
// Created by Kunal Sheth on 2/19/25.
//

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/util/assert.h>

#include "pattern_matching_trie.h"

namespace dreal
{
    std::vector<std::pair<std::vector<Formula>, std::shared_ptr<PatternMatchingTrie::substitutions_map>>>
    PatternMatchingTrie::find_matches(const std::set<Formula>& literals) {
        std::vector<std::pair<std::vector<Formula>, std::shared_ptr<substitutions_map>>> state{
                {{}, std::make_shared<substitutions_map>()}
            }, next_state;

        for (const auto& form : literals) {
            next_state.clear();
            for (const auto& [some_literals, subs] : state) {
                for (const auto& [new_literal, new_subs] : find_matches(form, subs)) {
                    // todo: check how bad this is.
                    // todo: check if using vector, and then converting to set, makes it faster. or just doing it all with set.
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
        const std::optional<std::shared_ptr<substitutions_map>>& substitutions_primer
    ) {
        const auto substitutions = substitutions_primer.value_or(std::make_shared<substitutions_map>());
        f_matches_vec matches;
        recMatchForm(f, f_root, substitutions, matches);
        return matches;
    }

    PatternMatchingTrie::e_matches_vec PatternMatchingTrie::find_matches(
        const Expression& e,
        const std::optional<std::shared_ptr<substitutions_map>>& substitutions_primer
    ) {
        const auto substitutions = substitutions_primer.value_or(std::make_shared<substitutions_map>());
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

    std::optional<std::shared_ptr<PatternMatchingTrie::substitutions_map>>
    PatternMatchingTrie::attempt_substitution(
        const std::shared_ptr<substitutions_map>& substitutions, const Variable& a, const Variable& aP
    ) {
        if (a.get_type() != aP.get_type())
            return {};

        // must be injective.
        const auto& [fwd, bwd] = *substitutions;
        const auto fit = fwd.find(a), bit = bwd.find(aP);
        if (fit == fwd.end() && bit == bwd.end()) {
            // DREAL_ASSERT(bit == bwd.end());
            auto copy = std::make_shared<substitutions_map>(*substitutions);
            // DREAL_ASSERT(copy.use_count() == 1 && substitutions.use_count() > 1);
            const bool success_fwd = copy->first.try_emplace(a, aP).second;
            DREAL_ASSERT(success_fwd);
            const bool success_bwd = copy->second.try_emplace(aP, a).second;
            DREAL_ASSERT(success_bwd);
            return copy;
        }
        else if (fit->second.equal_to(aP)) {
            DREAL_ASSERT(bit->second.equal_to(a));
            return {substitutions};
        }
        else
            return {};
    }
}
