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
#include "dreal/util/predicate_normalizer.h"
#include <dreal/symbolic/symbolic_formula_cell.h>

#include "dynamic_bitset.h"

namespace dreal
{
    std::vector<std::pair<std::set<Formula>, std::shared_ptr<PatternMatchingTrie::substitutions_map>>>
    PredicateNormalizer::FindSimilar(const std::set<Formula>& clause) const {
        for (const auto& f : clause) {
            const auto& atom = is_negation(f) ? get_operand(f) : f;
            // Learned clauses MUST be a collection of normalized literals.
            DREAL_ASSERT(is_equal_to(atom) || is_less_than(atom) || is_less_than_or_equal_to(atom) || is_forall(atom));
        }
        return trie.find_matches(clause);
    }

    Formula PredicateNormalizer::Convert(const Formula& f) {
        const auto it = cache.find(f);
        if (it == cache.cend()) {
            const auto result = VisitFormula<Formula>(this, f);
            cache.emplace(f, result);
            return result;
        }
        else return it->second;
    }

    Formula PredicateNormalizer::VisitFalse(const Formula& f) { return f; }
    Formula PredicateNormalizer::VisitTrue(const Formula& f) { return f; }
    Formula PredicateNormalizer::VisitVariable(const Formula& f) { return f; }

    Formula PredicateNormalizer::VisitEqualTo(const Formula& f) {
        trie.insert(f);
        trie.insert(!f);
        return f;
    }

    Formula PredicateNormalizer::VisitLessThan(const Formula& f) {
        trie.insert(f);
        trie.insert(!f);
        return f;
    }

    Formula PredicateNormalizer::VisitLessThanOrEqualTo(const Formula& f) {
        trie.insert(f);
        trie.insert(!f);
        return f;
    }

    Formula PredicateNormalizer::VisitForall(const Formula& f) {
        // todo: support this??
        // const auto fa = to_forall(f);
        // return forall(fa->get_quantified_variables(), Convert(fa->get_quantified_formula()));
        // trie.insert(f);
        return f;
    }

    Formula PredicateNormalizer::VisitNotEqualTo(const Formula& f) {
        const Expression& lhs{get_lhs_expression(f)};
        const Expression& rhs{get_rhs_expression(f)};
        return !Convert(lhs == rhs);
    }

    Formula PredicateNormalizer::VisitGreaterThan(const Formula& f) {
        const Expression& lhs{get_lhs_expression(f)};
        const Expression& rhs{get_rhs_expression(f)};
        return !Convert(lhs <= rhs);
    }

    Formula PredicateNormalizer::VisitGreaterThanOrEqualTo(const Formula& f) {
        const Expression& lhs{get_lhs_expression(f)};
        const Expression& rhs{get_rhs_expression(f)};
        return !Convert(lhs < rhs);
    }

    Formula PredicateNormalizer::VisitConjunction(const Formula& f) {
        return make_conjunction(map(get_operands(f),
                                    [this](const Formula& formula) { return this->Convert(formula); }));
    }

    Formula PredicateNormalizer::VisitDisjunction(const Formula& f) {
        return make_disjunction(map(get_operands(f),
                                    [this](const Formula& formula) { return this->Convert(formula); }));
    }

    Formula PredicateNormalizer::VisitNegation(const Formula& f) {
        return !Convert(get_operand(f));
    }
} // namespace dreal
