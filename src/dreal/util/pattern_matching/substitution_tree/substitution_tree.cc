//
// Created by Kunal Sheth on 12/27/25.
//

#include "substitution_tree.h"

#include <capd/autodiff/NodeType.h>

#include "dreal/util/assert.h"
#include "dreal/util/pattern_matching/substitutions_map.h"

namespace dreal
{
#define get_map(variant)  std::get<std::map<Variable, Node>>(variant)

    template <typename Leaf, typename Variable>
    template <class It>
    void substitution_tree<Leaf, Variable>::insert(const It& begin, const It& end, std::map<Variable, Node>& parents_next, const Leaf& leaf) {
        const Variable& var = *begin;
        It next = begin;
        ++next;
        if (next == end) {
            const auto [it, success] = parents_next.try_emplace(var, leaf);
            if (!success)
                DREAL_ASSERT(std::get<const Leaf>(it->second.next).EqualTo(leaf));
        }
        else {
            insert(next, end, get_map(parents_next[var].next/*constructs if necessary*/), leaf);
        }
    }

    template <typename Leaf, typename Variable>
    void substitution_tree<Leaf, Variable>::insert(const std::vector<Variable>& concrete_vars, const Leaf& leaf) {
        if (concrete_vars.empty()) {
            DREAL_ASSERT(!root.has_value());
            root.emplace(leaf);
        }
        else {
            if (!root.has_value()) root.emplace();
            insert(concrete_vars.begin(), concrete_vars.end(), get_map(root->next), leaf);
        }
    }

    template <typename Leaf, typename Variable>
    template <class It>
    void substitution_tree<Leaf, Variable>::find_matches(
        const It& begin, const It& end, substitutions_map& subs, const Node& parent,
        const matches_vec& matches, const misses_vec& misses
    ) const {
        if (begin == end) {
            matches(std::get<const Leaf>(parent.next), subs);
            return;
        } // else {

        const auto init_size = subs.size();

        const Variable& concrete_var = *begin;
        It next = begin;
        ++next;
        for (const auto& [matched_var, child] : get_map(parent.next)) {
            subs.push();
            auto status = subs.attempt_substitution(matched_var, concrete_var);
            if (status == substitutions_map::SUCCESS) {
                find_matches(next, end, subs, child, matches, misses);
            }
            else /*if (status != substitutions_map::SUCCESS)*/ {
                misses(status); // terminate descent path.
            }
            subs.pop();
        }

        DREAL_ASSERT(subs.size() == init_size);
    }

    template <typename Leaf, typename Variable>
    void substitution_tree<Leaf, Variable>::find_matches(
        const std::vector<Variable>& concrete_vars, substitutions_map& subs,
        const matches_vec& matches, const misses_vec& misses
    ) const {
        if (concrete_vars.empty()) {
            matches(std::get<const Leaf>(root->next), subs);
        }
        else {
            find_matches(concrete_vars.begin(), concrete_vars.end(), subs, *root, matches, misses);
        }
    }

    // Force template code generation into this translation unit.
    template class substitution_tree<Expression, Variable>;
    template class substitution_tree<Formula, Variable>;
}
