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

        static float calculate(statistics k);
    };
}

#endif //PATTERN_MATCHING_HEURISTIC_H
