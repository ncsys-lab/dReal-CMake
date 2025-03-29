//
// Created by Kunal Sheth on 2/19/25.
//

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/util/assert.h>

#include "pattern_matching_trie.h"

namespace dreal
{
    std::set<Formula> PatternMatchingTrie::find_matches(const Formula& f) {
        const auto substitutions = std::make_shared<substitutions_map>();
        f_matches_vec matches;
        recMatchForm(f, f_root, substitutions, matches);
        std::set<Formula> result;
        for (const auto& [term1, subs] : matches) {
            // { // todo: remove check
            //     ExpressionSubstitution es(subs->size());
            //     es.insert(subs->begin(), subs->end());
            //     const auto term2 = f.Substitute(es);
            //     DREAL_ASSERT(term1.EqualTo(term2));
            // }
            result.insert(term1);
        }
        return result;
    }

    std::set<Expression> PatternMatchingTrie::find_matches(const Expression& e) {
        const auto substitutions = std::make_shared<substitutions_map>();
        e_matches_vec matches;
        recMatchExpr(e, e_root, substitutions, matches);
        std::set<Expression> result;
        for (const auto& [term, subs] : matches) {
            { // todo: remove check
                ExpressionSubstitution es(subs->size());
                es.insert(subs->begin(), subs->end());
                DREAL_ASSERT(e.EqualTo(term.Substitute(es)));
            }
            result.insert(term);
        }
        return result;
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
        const auto it = substitutions->find(a);
        if (it == substitutions->end()) {
            auto copy = std::make_shared<substitutions_map>(*substitutions);
            // DREAL_ASSERT(copy.use_count() == 1 && substitutions.use_count() > 1);
            const bool success = copy->try_emplace(a, aP).second;
            DREAL_ASSERT(success);
            return copy;
        }
        else if (it->second.equal_to(aP))
            return {substitutions};
        else
            return {};
    }
}
