/*
   Copyright 2017 Toyota Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#pragma once

#include <unordered_map>
#include <vector>
#include <dreal/util/pattern_matching/pattern_matching_trie.h>

#include "predicate_heuristic.h"
#include "dreal/symbolic/symbolic.h"

namespace dreal
{
    class PredicateNormalizer
    {
    public:
        Formula Convert(const Formula& f);

        [[nodiscard]] std::pair<
            std::vector<std::pair<std::vector<Formula>, substitutions_map>>, PatternMatchingTrie::
            matching_stats_t
        >
        FindSimilar(
            const std::vector<Formula>& ordered_clause,
            std::chrono::duration<uint64_t, std::micro> timeout = std::chrono::microseconds{-1}
        ) const;

        // useless heuristic
        // uint64_t EstimateMatchingCost(const std::set<Formula>& f);

        PredicateHeuristic heuristic; // todo: make private?

    private:
        Formula VisitFalse(const Formula& f);
        Formula VisitTrue(const Formula& f);
        Formula VisitVariable(const Formula& f);
        Formula VisitEqualTo(const Formula& f);
        Formula VisitNotEqualTo(const Formula& f);
        Formula VisitGreaterThan(const Formula& f);
        Formula VisitGreaterThanOrEqualTo(const Formula& f);
        Formula VisitLessThan(const Formula& f);
        Formula VisitLessThanOrEqualTo(const Formula& f);
        Formula VisitConjunction(const Formula& f);
        Formula VisitDisjunction(const Formula& f);
        Formula VisitNegation(const Formula& f);
        Formula VisitForall(const Formula& f);

        std::unordered_map<Formula, Formula> cache;
        PatternMatchingTrie trie;

        friend Formula drake::symbolic::VisitFormula<
            Formula, PredicateNormalizer>(PredicateNormalizer*, const Formula&);
    };
} // namespace dreal
