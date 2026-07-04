//
// Created by Kunal Sheth on 4/1/25.
//

#ifndef substitutions_mapH
#define substitutions_mapH
#include "dreal/symbolic/symbolic_variable.h"
#include <dreal/symbolic/symbolic.h>
#include <dreal/util/box.h>
#include <dreal/version.h> // NOLINT(*-include-cleaner)
#include <utility>
#include <vector>

namespace dreal
{
    class substitutions_map
    {
    private:
        std::vector<std::pair<Variable, Variable>> mapping;
        std::vector<size_t> index_stack;
        const Box& box;

    public:
        [[nodiscard]] const std::vector<std::pair<Variable, Variable>>& get_map() const { return mapping; }

        substitutions_map(const Box& b, size_t reserved_size);

        // "forward" takes the matched and maps it to original
        // "backward" takes original and maps it to matched
        template <typename T>
        [[nodiscard]] static T apply_substitution(const T& f, const substitutions_map& subs, bool backward);

        void push();

        void pop();

        typedef enum
        {
            SUCCESS, STRUCTURE_MISS, INDICES_MISS, TYPE_MISS, BOX_MISS, BIJ_MISS, CONST_MISS,
#if CAV26_FILTER_SYMMETRIES
            CAV26_NOT_PURE_TIME, CAV26_NOT_PURE_LOGIC, CAV26_NOT_PURE_ANY,
#endif
            LEN_substitution_statuses
        } substitution_status;

        substitution_status attempt_substitution(const Variable& a, const Variable& aP);

        [[nodiscard]] size_t size() const;

        void reserve(size_t n);

        // very expensive, don't want to accidentally call somewhere.
        // friend bool operator==(const substitutions_map& lhs, const substitutions_map& rhs);
        // friend bool operator!=(const substitutions_map& lhs, const substitutions_map& rhs) { return !(lhs == rhs); }
    };
}

#endif //substitutions_mapH
