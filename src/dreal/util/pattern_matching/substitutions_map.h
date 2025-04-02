//
// Created by Kunal Sheth on 4/1/25.
//

#ifndef substitutions_map_nodeH
#define substitutions_map_nodeH
#include <utility>
#include <dreal/symbolic/symbolic.h>
#include <dreal/util/box.h>

namespace dreal
{
    class substitutions_map_node;
    using substitutions_map_ptr = std::shared_ptr<const substitutions_map_node>;

    class substitutions_map_node : public std::enable_shared_from_this<substitutions_map_node>
    {
    private:
        Variable a, aP;
        substitutions_map_ptr next;

        friend substitutions_map_ptr attempt_substitution(
            substitutions_map_ptr subs, const Variable& a, const Variable& aP
        );

    public:
        const size_t size;

        substitutions_map_node(
            Variable a, Variable aP, substitutions_map_ptr next
        ): a(std::move(a)), aP(std::move(aP)), next(std::move(next)), size(1 + (next == nullptr ? 0 : next->size)) {}

        substitutions_map_node() = delete;
        ~substitutions_map_node() = default;

        static substitutions_map_ptr create_map(const Variable& a, const Variable& aP) {
            return std::make_shared<substitutions_map_node>(a, aP, nullptr);
        }

        template <typename T>
        static T apply_substitution(
            const T& f, const substitutions_map_ptr& subs, bool backward
        ) {
            ExpressionSubstitution esub;
            FormulaSubstitution fsub;
            for (const auto& sub : *subs) {
                const auto aP = backward ? sub->a : sub->aP;
                const auto a = backward ? sub->aP : sub->a;
                if (a.get_type() == Variable::Type::BOOLEAN)
                    fsub.emplace(a, Formula{aP});
                else
                    esub.emplace(a, aP);
            }
            return f.Substitute(esub, fsub);
        }

        [[nodiscard]] static Box apply_substitution(
            const Box& b, const substitutions_map_ptr& subs, bool backward = false
        );

        [[nodiscard]] static substitutions_map_ptr attempt_substitution(
            substitutions_map_ptr subs, const Variable& a, const Variable& aP
        );

        class Iterator;
        class ConstIterator;

        [[nodiscard]] Iterator begin();
        [[nodiscard]] Iterator end();
        [[nodiscard]] ConstIterator begin() const;
        [[nodiscard]] ConstIterator end() const;

        class Iterator
        {
        private:
            substitutions_map_ptr current;

        public:
            explicit Iterator(substitutions_map_ptr current) : current{std::move(current)} {}
            substitutions_map_ptr operator*();
            Iterator& operator++();
            Iterator operator++(int);
            bool operator==(const Iterator& other) const;
            bool operator!=(const Iterator& other) const;
        };

        class ConstIterator
        {
        private:
            substitutions_map_ptr current;

        public:
            explicit ConstIterator(substitutions_map_ptr current): current{
                std::move(current)
            } {}

            substitutions_map_ptr operator*() const;
            ConstIterator& operator++();
            ConstIterator operator++(int);
            bool operator==(const ConstIterator& other) const;
            bool operator!=(const ConstIterator& other) const;
        };
    };
}

#endif //substitutions_map_nodeH
