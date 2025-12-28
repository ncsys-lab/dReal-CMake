//
// Created by Kunal Sheth on 12/27/25.
//

#ifndef DREAL4_CMAKE_SUBSTITUTION_TREE_H
#define DREAL4_CMAKE_SUBSTITUTION_TREE_H
#include <vector>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/pattern_matching/substitutions_map.h"


namespace dreal
{
    template <typename Leaf = Formula, typename Variable = Variable>
    class substitution_tree
    {
    private:
        class Node
        {
        public:
            Node() : next(std::map<Variable, Node>{}) {}
            explicit Node(const Leaf& leaf) : next(leaf) {}

            Node(const Node& other) = delete;
            Node(Node&& other) noexcept = delete;

            std::variant<
                std::map<Variable, Node>,
                const Leaf> next;
        };

        std::optional<Node> root;

        template <class It>
        static void insert(const It& begin, const It& end, std::map<Variable, Node>& parents_next, const Leaf& leaf);

        using matches_vec = std::function<void(const Leaf& e, substitutions_map& s)>;
        using misses_vec = std::function<void(const substitutions_map::substitution_status& s)>;

    public:
        void insert(const std::vector<Variable>& concrete_vars, const Leaf& leaf);

        template <class It>
        void find_matches(const It& begin, const It& end, substitutions_map& subs, const Node& parent,
                          const matches_vec& matches, const misses_vec& misses) const;
        void find_matches(const std::vector<Variable>& concrete_vars, substitutions_map& subs, const matches_vec& matches, const misses_vec& misses) const;
    };
}


#endif //DREAL4_CMAKE_SUBSTITUTION_TREE_H
