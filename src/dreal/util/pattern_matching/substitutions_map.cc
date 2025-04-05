//
// Created by Kunal Sheth on 4/1/25.
//

#include "substitutions_map.h"

#include <unordered_set>
#include <dreal/util/logging.h>

namespace dreal
{
    Box substitutions_map::apply_substitution(
        const Box& b, const substitutions_map& subs, bool backward
    ) {
        Box new_b;
        const auto& map = backward ? subs.bwd : subs.fwd;
        for (const auto& [a,aP] : map) {
            // if (new_b.has_variable(aP)) continue;
            const auto& ba = b[a];
            new_b.Add(aP, ba.lb(), ba.ub());
        }
        return new_b;
    }

    void substitutions_map::push() {
        insertion_stack.emplace_back();
    }

    void substitutions_map::pop() {
        for (const auto& [a,aP] : insertion_stack.back()) {
            DREAL_ASSERT(fwd.count(a) == 1);
            DREAL_ASSERT(bwd.count(aP) == 1);
            fwd.erase(a);
            bwd.erase(aP);
        }
        insertion_stack.pop_back();
    }

    bool substitutions_map::attempt_substitution(const Variable& a, const Variable& aP) {
        if (a.get_type() != aP.get_type()) return false;

        const auto it_a = fwd.find(a);
        const auto it_aP = bwd.find(aP);
        const bool has_a = it_a != fwd.end();
        const bool has_aP = it_aP != bwd.end();

        if (!has_a && !has_aP) { // do insert
            fwd[a] = aP;
            bwd[aP] = a;
            insertion_stack.back().emplace_back(a, aP);
            return true;
        }

        if (
            has_a && has_aP &&
            it_a->second.equal_to(aP) &&
            it_aP->second.equal_to(a)
        ) {
            return true;
        }

        DREAL_LOG_TRACE(
            "Substitution failed. New 'a' {} is already {}, while new 'aP' {} is already {}.",
            a.get_name(), it_aP->second.get_name(), aP.get_name(), it_a->second.get_name()
        );
        return false;
    }

    bool operator==(const substitutions_map& lhs, const substitutions_map& rhs) {
        if (lhs.fwd.size() != rhs.fwd.size()) return false;
        if (lhs.bwd.size() != rhs.bwd.size()) return false;

        for (const auto& [a, aP] : lhs.fwd) {
            const auto it = rhs.fwd.find(a);
            if (it == rhs.fwd.end() || it->second != a) return false;
        }

        for (const auto& [aP, a] : lhs.bwd) {
            const auto it = rhs.bwd.find(aP);
            if (it == rhs.bwd.end() || it->second != aP) return false;
        }

        return true;
    }
}
