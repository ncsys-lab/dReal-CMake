//
// Created by Kunal Sheth on 4/1/25.
//

#include "substitutions_map.h"

#include <unordered_set>
#include <dreal/util/logging.h>

namespace dreal
{
    std::optional<substitutions_map_ptr> substitutions_map_node::attempt_substitution(
        const substitutions_map_ptr& subs,
        const Variable& a, const Variable& aP
    ) {
        if (a.get_type() != aP.get_type())
            return {};

        // must be bijective.
        if (subs != nullptr)
            for (const auto& sub : *subs) {
                const bool is_a = sub->a.equal_to(a);
                const bool is_aP = sub->aP.equal_to(aP);
                if (!is_a && !is_aP) continue;
                if (is_a && is_aP) return subs;
                // one or the other is already mapped to something else... must be bijective.
                DREAL_LOG_INFO(
                    "Substitution failed. New 'a' {} is already {}, while new 'aP' {} is already {}.",
                    a.get_name(), sub->a.get_name(), aP.get_name(), sub->aP.get_name()
                );
                return {};
            }

        // mapping does not exist, create it!
        return std::make_shared<substitutions_map_node>(a, aP, subs);
    }

    bool substitutions_map_node::verify_substitutions(const substitutions_map_ptr& subs) {
        // if (subs == nullptr) return true;
        //
        // // must be bijective.
        // std::unordered_map<Variable, Variable> fwd, bwd;
        // for (const auto& sub : *subs) {
        //     const auto& a = sub->a;
        //     const auto& aP = sub->aP;
        //     const auto [fit, fsucc] = fwd.emplace(a, aP);
        //     const auto [bit, bsucc] = bwd.emplace(aP, a);
        //     if (fsucc && bsucc) continue; // new pairing.
        //     if (fit->second.equal_to(aP) && bit->second.equal_to(a)) continue; // exact pairing already exists.
        //     // one or the other is already mapped to something else... won't be bijective.
        //     DREAL_LOG_TRACE(
        //         "Substitution is invalid. Attempted to substitute ({}, {}), while fwd({}, {}) and bwd({}, {}) already exist.",
        //         a.get_name(), aP.get_name(),
        //         fit->first.get_name(), fit->second.get_name(),
        //         bit->first.get_name(), bit->second.get_name()
        //     );
        //     return false;
        // }
        return true;
    }

    Box substitutions_map_node::apply_substitution(
        const Box& b, const substitutions_map_ptr& subs, bool backward
    ) {
        Box new_b;
        for (const auto& sub : *subs) {
            const auto aP = backward ? sub->aP : sub->a;
            const auto a = backward ? sub->a : sub->aP;
            const auto& ba = b[a];
            new_b.Add(aP, ba.lb(), ba.ub());
        }
        return new_b;
    }

    substitutions_map_ptr substitutions_map_node::Iterator::operator*() { return current; }

    substitutions_map_node::Iterator& substitutions_map_node::Iterator::operator++() {
        if (current) current = current->next;
        return *this;
    }

    substitutions_map_node::Iterator substitutions_map_node::Iterator::operator++(int) {
        Iterator temp = *this;
        ++(*this);
        return temp;
    }

    bool substitutions_map_node::Iterator::operator==(const Iterator& other) const { return current == other.current; }
    bool substitutions_map_node::Iterator::operator!=(const Iterator& other) const { return !(*this == other); }

    substitutions_map_ptr substitutions_map_node::ConstIterator::operator*() const { return current; }

    substitutions_map_node::ConstIterator& substitutions_map_node::ConstIterator::operator++() {
        if (current) current = current->next;
        return *this;
    }

    substitutions_map_node::ConstIterator substitutions_map_node::ConstIterator::operator++(int) {
        ConstIterator temp = *this;
        ++(*this);
        return temp;
    }

    bool substitutions_map_node::ConstIterator::operator==(const ConstIterator& other) const {
        return current == other.current;
    }

    bool substitutions_map_node::ConstIterator::operator!=(const ConstIterator& other) const {
        return !(*this == other);
    }

    substitutions_map_node::Iterator substitutions_map_node::begin() { return Iterator(shared_from_this()); }
    substitutions_map_node::Iterator substitutions_map_node::end() { return Iterator(nullptr); }

    substitutions_map_node::ConstIterator substitutions_map_node::begin() const {
        return ConstIterator(shared_from_this());
    }

    substitutions_map_node::ConstIterator substitutions_map_node::end() const { return ConstIterator(nullptr); }
}
