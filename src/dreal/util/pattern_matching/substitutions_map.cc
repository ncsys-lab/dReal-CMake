//
// Created by Kunal Sheth on 4/1/25.
//

#include "substitutions_map.h"

#include <dreal/util/logging.h>

namespace dreal
{
    substitutions_map_ptr attempt_substitution(
        substitutions_map_ptr subs,
        const Variable& a, const Variable& aP
    ) {
        if (a.get_type() != aP.get_type())
            return {};

        // must be bijective.
        if (subs != nullptr) for (const auto& sub : *subs) {
            const bool is_a = sub->a.equal_to(a);
            const bool is_aP = sub->aP.equal_to(aP);
            if (!is_a && !is_aP) continue;
            if (is_a && is_aP) return subs;
            // one or the other is already mapped to something else... must be bijective.
            DREAL_LOG_TRACE(
                "Substitution failed. new 'a' {} is already {}, while new 'aP' {} is already {}",
                a.get_name(), sub->a.get_name(), aP.get_name(), sub->aP.get_name()
            );
            return {};
        }
        // mapping does not exist, create it!
        return std::make_shared<substitutions_map_node>(a, aP, subs);
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
