//
// Created by Kunal Sheth on 4/1/25.
//

#ifndef substitutions_mapH
#define substitutions_mapH
#include <dreal/symbolic/symbolic.h>
#include <dreal/util/box.h>
#include <dreal/util/exception.h>

namespace dreal
{
    class substitutions_map : public std::enable_shared_from_this<substitutions_map>
    {
    private:
        std::unordered_map<Variable, Variable> fwd;
        std::unordered_map<Variable, Variable> bwd;
        std::vector<std::vector<std::pair<Variable, Variable>>> insertion_stack{1};
        const Box& box;

    public:
        const std::unordered_map<Variable, Variable>& get_map() const { return fwd; }
        const std::vector<std::pair<Variable, Variable>>& get_current_frame() const { return insertion_stack.back(); }

        // substitutions_map();
        // explicit substitutions_map(Box b);
        // explicit substitutions_map(size_t reserve);
        substitutions_map(const Box& b, size_t reserved_size);

        // "forward" takes the matched and maps it to original
        // "backward" takes original and maps it to matched
        template <typename T>
        [[nodiscard]] static T apply_substitution(
            const T& f, const substitutions_map& subs, bool backward
        ) {
            ExpressionSubstitution esub;
            FormulaSubstitution fsub;
            const auto& map = backward ? subs.bwd : subs.fwd;
            for (const auto& [a,aP] : map) {
                if (a.get_type() == Variable::Type::BOOLEAN)
                    fsub.emplace(a, Formula{aP});
                else
                    esub.emplace(a, aP);
            }
            return f.Substitute(esub, fsub);
        }

        // [[nodiscard]] static Box apply_substitution(
        // const Box& b, const substitutions_map& subs, bool backward = false
        // );

        void push();

        void pop();

        typedef enum
        {
            SUCCESS, TYPE_MISS, BOX_MISS, BIJ_MISS, CONST_MISS
        } substitution_status;

        substitution_status attempt_substitution(const Variable& a, const Variable& aP);

        size_t size() const;

        void reserve(size_t n);

        friend bool operator==(const substitutions_map& lhs, const substitutions_map& rhs);

        friend bool operator!=(const substitutions_map& lhs, const substitutions_map& rhs) { return !(lhs == rhs); }
    };
}

#endif //substitutions_mapH