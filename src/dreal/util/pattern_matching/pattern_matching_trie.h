//
// Created by Kunal Sheth on 2/19/25.
//

#ifndef pattern_matching_trie_H
#define pattern_matching_trie_H

#include <vector>
#include <dreal/util/assert.h>
#include <dreal/util/exception.h>
#include <dreal/util/pattern_matching/substitutions_map.h>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal
{
    class PatternMatchingTrie
    {
    public:
        using e_matches_vec = std::function<void(const Expression& e, substitutions_map& s)>;
        using f_matches_vec = std::function<void(const Formula& f, substitutions_map& s)>;

        [[nodiscard]] std::vector<std::pair<std::vector<Formula>, substitutions_map>>
        find_matches(
            const std::set<Formula>& literals
        ) const;
        [[nodiscard]] std::vector<std::pair<Formula, substitutions_map>> find_matches(
            const Formula& f,
            const substitutions_map& substitutions = {}
        ) const;
        [[nodiscard]] std::vector<std::pair<Expression, substitutions_map>> find_matches(
            const Expression& f,
            const substitutions_map& substitutions = {}
        ) const;

        void insert(const Formula& f);
        void insert(const Expression& f);

        // private:
        template <typename TKind, typename This, typename OKind, typename Other>
        class TrieNode
        {
        public:
            TrieNode(): children{4} {}

            TrieNode(
                const std::optional<This>& leaf,
                const std::optional<This>& terminal_expression = {}
            ): switch_kind{}, children{4}, leaf{leaf}, terminal_expression{terminal_expression} {}

            TrieNode(const TrieNode& other) = delete; // these should never be copied.

            TrieNode(TrieNode&& other) noexcept
                : switch_kind{std::move(other.switch_kind)},
                  children{std::move(other.children)},
                  leaf{std::move(other.leaf)},
                  terminal_expression{std::move(other.terminal_expression)} {}

            ~TrieNode() noexcept {
                if (switch_kind != nullptr) delete switch_kind;
            }

            [[nodiscard]] TrieNode<OKind, Other, TKind, This>& init_switch_kind() {
                if (switch_kind == nullptr) switch_kind = new TrieNode<OKind, Other, TKind, This>;
                return *switch_kind;
            }

            [[nodiscard]] const std::vector<TrieNode<TKind, This, OKind, Other>>& c(const TKind& k) const {
                const auto it = children.find(k);
                if (it != children.end()) return it->second;
                const static std::vector<TrieNode<TKind, This, OKind, Other>> t_empty;
                return t_empty;
            }

            [[nodiscard]] std::vector<TrieNode<TKind, This, OKind, Other>>& c(const TKind& k) {
                return children[k];
            }

            [[nodiscard]] const TrieNode<TKind, This, OKind, Other>& c_leafless(const TKind& k) const {
                const auto& vec = c(k);
                DREAL_ASSERT(vec.size() == 1);
                return vec[0];
            }

            [[nodiscard]] TrieNode<TKind, This, OKind, Other>& c_leafless(const TKind& k) {
                auto& vec = c(k);
                if (vec.empty()) vec.emplace_back();
                DREAL_ASSERT(vec.size() == 1);
                return vec[0];
            }

            std::optional<This> leaf{};
            std::optional<This> terminal_expression{};
            // important for ITEs which contain both Formulas and Expressions
            TrieNode<OKind, Other, TKind, This>* switch_kind = nullptr;

        private:
            std::unordered_map<TKind, std::vector<TrieNode>> children;
            // friend PatternMatchingTrie;
        };

        using ExprNode = TrieNode<ExpressionKind, Expression, FormulaKind, Formula>;
        using FormNode = TrieNode<FormulaKind, Formula, ExpressionKind, Expression>;
        ExprNode e_root;
        FormNode f_root;

        // using e_partial_matches_vec = std::vector<std::pair<const ExprNode*, substitutions_map>>;
        using e_partial_matches_vec = std::function<void(const ExprNode& n, substitutions_map& s)>;
        // using f_partial_matches_vec = std::vector<std::pair<const FormNode*, substitutions_map>>;
        using f_partial_matches_vec = std::function<void(const FormNode& n, substitutions_map& s)>;

        using e_misses_vec = std::function<void(const substitutions_map &s)>;
        using f_misses_vec = std::function<void(const substitutions_map &s)>;

#define PM_CONT_LAMBDA(n,s) [&](const auto &n, auto &s)

#define VISIT_DECL(name) void name ( \
    const Expression &_e, const ExprNode &parent, \
    substitutions_map &substitutions, \
    const e_matches_vec &matches, \
    const e_partial_matches_vec &partial_matches, \
    const e_misses_vec &misses \
) const
#define ADD_DECL(name) ExprNode& name (const Expression &e, ExprNode &parent, const std::optional<Expression> &is_terminal)
#define VISIT_AND_ADD_DECL(name) \
        VISIT_DECL(name); \
        ADD_DECL(name);
        VISIT_AND_ADD_DECL(VisitVariable);
        VISIT_AND_ADD_DECL(VisitConstant);
        VISIT_AND_ADD_DECL(VisitRealConstant);
        VISIT_AND_ADD_DECL(VisitAddition);
        VISIT_AND_ADD_DECL(VisitMultiplication);
        VISIT_AND_ADD_DECL(VisitDivision);
        VISIT_AND_ADD_DECL(VisitLog);
        VISIT_AND_ADD_DECL(VisitAbs);
        VISIT_AND_ADD_DECL(VisitExp);
        VISIT_AND_ADD_DECL(VisitSqrt);
        VISIT_AND_ADD_DECL(VisitPow);
        VISIT_AND_ADD_DECL(VisitSin);
        VISIT_AND_ADD_DECL(VisitCos);
        VISIT_AND_ADD_DECL(VisitTan);
        VISIT_AND_ADD_DECL(VisitAsin);
        VISIT_AND_ADD_DECL(VisitAcos);
        VISIT_AND_ADD_DECL(VisitAtan);
        VISIT_AND_ADD_DECL(VisitAtan2);
        VISIT_AND_ADD_DECL(VisitSinh);
        VISIT_AND_ADD_DECL(VisitCosh);
        VISIT_AND_ADD_DECL(VisitTanh);
        VISIT_AND_ADD_DECL(VisitMin);
        VISIT_AND_ADD_DECL(VisitMax);
        VISIT_AND_ADD_DECL(VisitIfThenElse);
        VISIT_AND_ADD_DECL(VisitUninterpretedFunction);
#undef VISIT_DECL
#undef ADD_DECL
#undef VISIT_AND_ADD_DECL

#define VISIT_DECL(name) void name ( \
    const Formula &f, const FormNode &parent, \
    substitutions_map &substitutions, \
    const f_matches_vec &matches, \
    const f_partial_matches_vec& partial_matches, \
    const f_misses_vec& misses \
) const
#define ADD_DECL(name) FormNode& name (const Formula &f, FormNode &parent, const std::optional<Formula> &is_terminal)
#define VISIT_AND_ADD_DECL(name) \
        VISIT_DECL(name); \
        ADD_DECL(name);
        VISIT_AND_ADD_DECL(VisitFalse);
        VISIT_AND_ADD_DECL(VisitTrue);
        VISIT_AND_ADD_DECL(VisitVariable);
        VISIT_AND_ADD_DECL(VisitEqualTo);
        VISIT_AND_ADD_DECL(VisitNotEqualTo);
        VISIT_AND_ADD_DECL(VisitGreaterThan);
        VISIT_AND_ADD_DECL(VisitGreaterThanOrEqualTo);
        VISIT_AND_ADD_DECL(VisitLessThan);
        VISIT_AND_ADD_DECL(VisitLessThanOrEqualTo);
        VISIT_AND_ADD_DECL(VisitConjunction);
        VISIT_AND_ADD_DECL(VisitDisjunction);
        VISIT_AND_ADD_DECL(VisitNegation);
        VISIT_AND_ADD_DECL(VisitForall);
#undef VISIT_DECL
#undef ADD_DECL
#undef VISIT_AND_ADD_DECL

        void UnaryOpMatchHelper(const Expression& e, const ExpressionKind& k, const ExprNode& parent,
                                substitutions_map& substitutions,
                                const e_matches_vec& matches,
                                const e_partial_matches_vec& partial_matches,
                                const e_misses_vec& misses) const;
        void UnaryOpMatchHelper(const Formula& e, const FormulaKind& k, const FormNode& parent,
                                substitutions_map& substitutions,
                                const f_matches_vec& matches,
                                const f_partial_matches_vec& partial_matches,
                                const f_misses_vec& misses) const;
        void BinaryOpMatchHelper(const Expression& e, const ExpressionKind& k, const ExprNode& parent,
                                 substitutions_map& substitutions,
                                 const e_matches_vec& matches,
                                 const e_partial_matches_vec& partial_matches,
                                 const e_misses_vec& misses) const;
        void BinaryOpMatchHelper(const Formula& f, const FormulaKind& k, const FormNode& parent,
                                 substitutions_map& substitutions,
                                 const f_matches_vec& matches,
                                 const f_partial_matches_vec& partial_matches,
                                 const f_misses_vec& misses) const;
        void NaryOpMatchHelper(const Formula& _f, const FormulaKind& k, const FormNode& parent,
                               substitutions_map& s1,
                               const f_matches_vec& matches,
                               const f_partial_matches_vec& partial_matches,
                               const f_misses_vec& misses) const;
        ExprNode& UnaryOpAddHelper(const Expression& f, const ExpressionKind& k, ExprNode& parent,
                                   const std::optional<Expression>& is_terminal);
        FormNode& UnaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                   const std::optional<Formula>& is_terminal);
        ExprNode& BinaryOpAddHelper(const Expression& f, const ExpressionKind& k, ExprNode& parent,
                                    const std::optional<Expression>& is_terminal);
        FormNode& BinaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                    const std::optional<Formula>& is_terminal);
        FormNode& NaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                  const std::optional<Formula>& is_terminal);

        void recMatchExpr(
            const Expression& e, const ExprNode& parent, substitutions_map& substitutions,
            const e_matches_vec& matches, const e_partial_matches_vec& partial_matches, const e_misses_vec& misses
        ) const {
            // std::cout << "recMatchExpr: " << e << std::endl;
            return VisitExpression<void>(this, e, parent, substitutions, matches, partial_matches, misses);
        }

        ExprNode& recAddExpr(
            const Expression& e, ExprNode& parent, const std::optional<Expression>& is_terminal
        ) { return VisitExpression<ExprNode&>(this, e, parent, is_terminal); }

        void recMatchForm(
            const Formula& f, const FormNode& parent, substitutions_map& substitutions,
            const f_matches_vec& matches, const f_partial_matches_vec& partial_matches, const f_misses_vec& misses
        ) const {
            // std::cout << "recMatchForm: " << f << std::endl;
            return VisitFormula<void>(this, f, parent, substitutions, matches, partial_matches, misses);
        }

        FormNode& recAddForm(
            const Formula& f, FormNode& parent, const std::optional<Formula>& is_terminal
        ) { return VisitFormula<FormNode&>(this, f, parent, is_terminal); }

        friend void drake::symbolic::VisitExpression<void>(
            PatternMatchingTrie*, const Expression& e,
            const ExprNode& parent, substitutions_map& substitutions,
            const e_matches_vec& matches, const e_partial_matches_vec& partial_matches, const e_misses_vec& misses
        );
        friend ExprNode& drake::symbolic::VisitExpression<ExprNode&>(
            PatternMatchingTrie*, const Expression& e, ExprNode& parent,
            const std::optional<Expression>& is_terminal
        );
        friend void drake::symbolic::VisitFormula<void>(
            PatternMatchingTrie*, const Formula& e,
            const FormNode& parent, substitutions_map& substitutions,
            const f_matches_vec& matches, const f_partial_matches_vec& partial_matches, const f_misses_vec& misses
        );
        friend FormNode& drake::symbolic::VisitFormula<FormNode&>(
            PatternMatchingTrie*, const Formula& e, FormNode& parent,
            const std::optional<Expression>& is_terminal
        );
    };
} // namespace dreal


#endif //pattern_matching_trie_H
