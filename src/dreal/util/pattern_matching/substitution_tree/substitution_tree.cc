//
// Created by Kunal Sheth on 12/27/25.
//

#include "substitution_tree.h"

#include <utility>
#include <vector>

#include "dreal/util/assert.h"
#include "dreal/util/pattern_matching/substitutions_map.h"
#include "dreal/version.h"
#include "dreal/util/math.h"

namespace dreal
{
#define get_map(variant)  std::get<std::vector<std::pair<Variable, Node>>>( (variant) )

    // "std::map::try_emplace" but for vector-of-pairs.
    // todo: should I just implement a vector-backed map structure?
    // this type of thing is also used in substitutions_map.cc and probably would be useful elsewhere too.
    template <typename Leaf, typename Variable, typename Node = typename substitution_tree<Leaf, Variable>::Node>
    void vecmap_try_emplace(
        std::vector<std::pair<Variable, Node>>& map,
        const Variable& new_var, const Leaf& new_leaf
    ) {
        for (const auto& [v,n] : map) {
            if (new_var.equal_to(v)) {
                DREAL_ASSERT(std::get<const Leaf>(n.next).EqualTo(new_leaf));
                return;
            }
        }
        map.emplace_back(new_var, new_leaf);
    }

    // `std::map[key] /*constructs if necessary*/` but for vector-of-pairs.
    template <typename Leaf, typename Variable, typename Node = typename substitution_tree<Leaf, Variable>::Node>
    Node& vecmap_backets(
        std::vector<std::pair<Variable, Node>>& map, const Variable& new_var
    ) {
        for (auto& [v,n] : map) if (new_var.equal_to(v)) return n;
        return map.emplace_back(new_var, Node{}).second;
    }


    template <typename Leaf, typename Variable>
    template <class It>
    void substitution_tree<Leaf, Variable>::insert(const It& begin, const It& end, std::vector<std::pair<Variable, Node>>& parents_next, const Leaf& leaf) {
        const Variable& var = *begin;
        It next = begin;
        ++next;
        if (next == end) {
            vecmap_try_emplace(parents_next, var, leaf);
            // const auto [it, success] = parents_next.try_emplace(var, leaf);
            // if (!success) DREAL_ASSERT(std::get<const Leaf>(it->second.next).EqualTo(leaf));
        }
        else {
            auto& lu = vecmap_backets<Leaf, Variable>(parents_next, var);
            insert(next, end, get_map(lu.next/*constructs if necessary*/), leaf);
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
        const matches_vec& matches, const misses_vec& misses, const unsigned starting_offset
    ) const {
        if (begin == end) {
            matches(std::get<const Leaf>(parent.next), subs);
            return;
        } // else {

        const auto init_size = subs.size();

        const Variable& concrete_var = *begin;
        It next = begin;
        ++next;

        const auto& map = get_map(parent.next);
        const auto N = map.size();
        auto i = N >= 2 ? starting_offset % N : 0;
        for (int _i = 0; _i < N; ++_i) {
            if (++i >= N) i = 0;
            DREAL_ASSERT((0 <= i) && (i < N));
            const auto& [matched_var, child] = map[i];

            subs.push();
            auto status = subs.attempt_substitution(matched_var, concrete_var);
            if (status == substitutions_map::SUCCESS) {
                find_matches(next, end, subs, child, matches, misses, starting_offset);
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
        const matches_vec& matches, const misses_vec& misses, uint64_t &random_state
    ) const {
        if (concrete_vars.empty()) {
            matches(std::get<const Leaf>(root->next), subs);
        }
        else {
            const unsigned random_offset = DREAL_EXPERIMENTAL_PM_SUBSTREE_RANDOMIZE ? fast_random_next(random_state) : 0;
            find_matches(concrete_vars.begin(), concrete_vars.end(), subs, *root, matches, misses, random_offset);
        }
    }

    // Force template code generation into this translation unit.
    template class substitution_tree<Expression, Variable>;
    template class substitution_tree<Formula, Variable>;
}
