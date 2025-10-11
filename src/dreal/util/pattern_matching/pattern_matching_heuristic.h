//
// Created by Kunal Sheth on 4/13/25.
//

#ifndef PATTERN_MATCHING_HEURISTIC_H
#define PATTERN_MATCHING_HEURISTIC_H
#include <dreal/util/predicate_heuristic.h>

#include "pattern_matching_trie.h"

namespace dreal
{
    class PatternMatchingHeuristic
    {
    public:
        using statistics = struct
        {
            unsigned box_continuous_count;
            unsigned box_integer_count;
            unsigned box_boolean_count;
            unsigned assertions_size;
            PredicateHeuristic::predicate_stats_t assertions_stats;
            PredicateHeuristic::predicate_stats_t biggest_assertion_stats;
            PredicateHeuristic::predicate_stats_t middle_assertion_stats;
            PredicateHeuristic::predicate_stats_t smallest_assertion_stats;
            double theory_checksat_ms;
            unsigned lemma_size;
            PredicateHeuristic::predicate_stats_t lemma_stats;
            PredicateHeuristic::predicate_stats_t biggest_literal_stats;
            PredicateHeuristic::predicate_stats_t middle_literal_stats;
            PredicateHeuristic::predicate_stats_t smallest_literal_stats;
            uint64_t estimated_matching_cost;
            double pattern_match_ms;
            PatternMatchingTrie::matching_stats_t pattern_matching_stats;
        };

        // todo: I have no idea why, but passing const ref instead of copying BREAKS it?!?!?!
        // it goes from functioning perfectly normally.. to just returning 0 and only 0...
        // the unit tests work, it only breaks the executable.
        // I think it's a compiler bug.... the struct is massive, the function is pure / constexpr.
        // lots to optimize away here.
        // honestly passing by copy might let some fancier optimizations through so who knows.
        static float calculate(const statistics& k);
    };
}

#endif //PATTERN_MATCHING_HEURISTIC_H
