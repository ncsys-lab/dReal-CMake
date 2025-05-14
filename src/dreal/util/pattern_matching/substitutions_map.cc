//
// Created by Kunal Sheth on 4/1/25.
//

#include "substitutions_map.h"

#include <unordered_set>
#include <utility>
#include <dreal/util/assert.h>
#include <dreal/util/logging.h>

namespace dreal
{
    substitutions_map::substitutions_map(const Box& b, const size_t reserved_size): box(b) {
        fwd.max_load_factor(0.25);
        bwd.max_load_factor(0.25);
        reserve(reserved_size);
    }

    // substitutions_map::substitutions_map(const size_t reserved_size) : substitutions_map() {
    //     reserve(reserved_size);
    // }

    // "forward" takes the matched and maps it to original
    // "backward" takes original and maps it to matched
    // Box substitutions_map::apply_substitution(
    //     const Box& b, const substitutions_map& subs, bool backward
    // ) {
    //     Box new_b;
    //     const auto& map = backward ? subs.bwd : subs.fwd;
    //     for (const auto& [a,aP] : map) {
    //         // if (new_b.has_variable(aP)) continue;
    //         const auto& ba = b[a];
    //         new_b.Add(aP, ba.lb(), ba.ub());
    //     }
    //     return new_b;
    // }

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

    substitutions_map::substitution_status substitutions_map::attempt_substitution(
        const Variable& a, const Variable& aP
    ) {
        if (a.get_type() != aP.get_type()) return TYPE_MISS;
        if (box[a] != box[aP]) {
            // todo: subset / superset ?
            return BOX_MISS;
        }

        const auto it_a = fwd.find(a);
        const auto it_aP = bwd.find(aP);
        const bool has_a = it_a != fwd.end();
        const bool has_aP = it_aP != bwd.end();

        if (!has_a && !has_aP) { // do insert
            fwd.try_emplace(a, aP);
            bwd.try_emplace(aP, a);
            insertion_stack.back().emplace_back(a, aP);
            return SUCCESS;
        }

        if (
            has_a && has_aP &&
            it_a->second.equal_to(aP) &&
            it_aP->second.equal_to(a)
        ) {
            return SUCCESS;
        }

        DREAL_LOG_TRACE(
            "Substitution failed. New 'a' {} is {}, while new 'aP' {} is {}.",
            a.get_name(),
            has_aP ? it_aP->second.get_name() : "unmapped",
            aP.get_name(),
            has_a ? it_a->second.get_name() : "unmapped"
        );
        return BIJ_MISS;
    }

    // this is literally the hottest function in the entire code rn, when we pattern match aggressively. -KS
    // ended up being actually slower??  a little.
    /*
    bool substitutions_map::attempt_substitution(const Variable& a, const Variable& aP) {
        if (a.get_type() != aP.get_type()) return false;

        // Attempt to find 'a' and 'aP' just once
        const auto [a_iterator, a_was_inserted] = fwd.try_emplace(a, aP);
        if (a_was_inserted) {
            // Only try to insert into bwd if we successfully inserted into fwd
            const auto [aP_iterator, aP_was_inserted] = bwd.try_emplace(aP, a);
            if (aP_was_inserted) {
                insertion_stack.back().emplace_back(a, aP);
                return true;
            }
            else {
                // Rollback fwd insertion if bwd insertion failed
                fwd.erase(a_iterator);
                return false;
            }
        }
        else {
            // Check if existing values match
            const auto aP_iterator = bwd.find(aP);
            if (
                aP_iterator != bwd.end() &&
                a_iterator->second.equal_to(aP) &&
                aP_iterator->second.equal_to(a)
            ) {
                return true;
            }

            DREAL_LOG_TRACE(
                "Substitution failed. New 'a' {} is already {}, while new 'aP' {} is already {}.",
                a.get_name(), aP_iterator != bwd.end() ? aP_iterator->second.get_name() : "<?>",
                aP.get_name(), a_iterator->second.get_name()
            );
            return false;
        }
        DREAL_UNREACHABLE();
    }
    */

    size_t substitutions_map::size() const {
        DREAL_ASSERT(fwd.size() == bwd.size());
        return fwd.size();
    }

    void substitutions_map::reserve(const size_t n) {
        fwd.reserve(n);
        bwd.reserve(n);
        insertion_stack.reserve(n);
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
