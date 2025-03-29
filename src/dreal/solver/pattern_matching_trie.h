//
// Created by Kunal Sheth on 2/19/25.
//

#ifndef pattern_matching_trie_H
#define pattern_matching_trie_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <dreal/symbolic/symbolic_expression_cell.h>
#include <dreal/util/assert.h>
#include <dreal/util/exception.h>

#include "dreal/symbolic/symbolic.h"

namespace dreal
{
    class PatternMatchingTrie
    {
    public:
        std::set<Formula> find_matches(const Formula& f);
        std::set<Expression> find_matches(const Expression& f);
        void insert(const Formula& f);
        void insert(const Expression& f);

        // private:
        template <typename TKind, typename This, typename OKind, typename Other>
        class TrieNode
        {
        public:
            TrieNode(): c{4} {}

            TrieNode(
                const std::optional<This>& leaf,
                const std::optional<This>& terminal_expression = {}
            ): switch_kind{}, c{4}, leaf{leaf}, terminal_expression{terminal_expression} {}

            TrieNode(const TrieNode& other) = default;

            TrieNode(TrieNode&& other) noexcept
                : switch_kind{std::move(other.switch_kind)},
                  c{std::move(other.c)},
                  leaf{std::move(other.leaf)},
                  terminal_expression{std::move(other.terminal_expression)} {}

            ~TrieNode() noexcept {
                if (switch_kind != nullptr) delete switch_kind;
            };

        private:
            TrieNode<OKind, Other, TKind, This>& init_switch_kind() {
                if (switch_kind == nullptr) switch_kind = new TrieNode<OKind, Other, TKind, This>;
                return *switch_kind;
            }

            TrieNode<TKind, This, OKind, Other>& c_leafless(const TKind& k) {
                auto& vec = c[k];
                if (vec.empty()) vec.emplace_back();
                DREAL_ASSERT(vec.size() == 1);
                return vec.back();
            }

            // important for ITEs which contain both Formulas and Expressions
            TrieNode<OKind, Other, TKind, This>* switch_kind = nullptr;
            std::unordered_map<TKind, std::vector<TrieNode>> c;
            std::optional<This> leaf{};
            std::optional<This> terminal_expression{};
            friend PatternMatchingTrie;
        };

        using ExprNode = TrieNode<ExpressionKind, Expression, FormulaKind, Formula>;
        using FormNode = TrieNode<FormulaKind, Formula, ExpressionKind, Expression>;
        ExprNode e_root;
        FormNode f_root;

        using substitutions_map = std::map<Variable, Variable>;
        using e_matches_vec = std::vector<std::pair<Expression, std::shared_ptr<substitutions_map>>>;
        using e_partial_matches_vec = std::vector<std::pair<ExprNode*, std::shared_ptr<substitutions_map>>>;
        using f_matches_vec = std::vector<std::pair<Formula, std::shared_ptr<substitutions_map>>>;
        using f_partial_matches_vec = std::vector<std::pair<FormNode*, std::shared_ptr<substitutions_map>>>;

        static std::optional<std::shared_ptr<substitutions_map>> attempt_substitution(
            const std::shared_ptr<substitutions_map>& substitutions,
            const Variable& a,
            const Variable& aP
        );

#define VISIT_DECL(name) e_partial_matches_vec name ( \
    const Expression &_e, ExprNode &parent, \
    const std::shared_ptr<substitutions_map> &substitutions, e_matches_vec &matches \
)
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

#define VISIT_DECL(name) f_partial_matches_vec name ( \
    const Formula &f, FormNode &parent, \
    const std::shared_ptr<substitutions_map> &substitutions, f_matches_vec &matches \
)
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

        e_partial_matches_vec BinaryOpMatchHelper(const Expression& e, const ExpressionKind& k, ExprNode& parent,
                                                  const std::shared_ptr<substitutions_map>& substitutions,
                                                  e_matches_vec& matches);
        f_partial_matches_vec BinaryOpMatchHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                                  const std::shared_ptr<substitutions_map>& substitutions,
                                                  f_matches_vec& matches);
        e_partial_matches_vec UnaryOpMatchHelper(const Expression& e, const ExpressionKind& k, ExprNode& parent,
                                                 const std::shared_ptr<substitutions_map>& substitutions,
                                                 e_matches_vec& matches);
        f_partial_matches_vec UnaryOpMatchHelper(const Formula& e, const FormulaKind& k, FormNode& parent,
                                                 const std::shared_ptr<substitutions_map>& substitutions,
                                                 f_matches_vec& matches);
        ExprNode& BinaryOpAddHelper(const Expression& f, const ExpressionKind& k, ExprNode& parent,
                                    const std::optional<Expression>& is_terminal);
        FormNode& BinaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                    const std::optional<Formula>& is_terminal);
        ExprNode& UnaryOpAddHelper(const Expression& f, const ExpressionKind& k, ExprNode& parent,
                                   const std::optional<Expression>& is_terminal);
        FormNode& UnaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                   const std::optional<Formula>& is_terminal);

        inline e_partial_matches_vec recMatchExpr(
            const Expression& e, ExprNode& parent, const std::shared_ptr<substitutions_map>& substitutions,
            e_matches_vec& matches
        ) { return VisitExpression<e_partial_matches_vec>(this, e, parent, substitutions, matches); }

        inline ExprNode& recAddExpr(
            const Expression& e, ExprNode& parent, const std::optional<Expression>& is_terminal
        ) { return VisitExpression<ExprNode&>(this, e, parent, is_terminal); }

        inline f_partial_matches_vec recMatchForm(
            const Formula& f, FormNode& parent, const std::shared_ptr<substitutions_map>& substitutions,
            f_matches_vec& matches
        ) { return VisitFormula<f_partial_matches_vec>(this, f, parent, substitutions, matches); }

        inline FormNode& recAddForm(
            const Formula& f, FormNode& parent, const std::optional<Formula>& is_terminal
        ) { return VisitFormula<FormNode&>(this, f, parent, is_terminal); }

        friend e_partial_matches_vec drake::symbolic::VisitExpression<e_partial_matches_vec>(
            PatternMatchingTrie*, const Expression& e,
            ExprNode& parent, const std::shared_ptr<substitutions_map>& substitutions, e_matches_vec& matches
        );
        friend ExprNode& drake::symbolic::VisitExpression<ExprNode&>(
            PatternMatchingTrie*, const Expression& e, ExprNode& parent,
            const std::optional<Expression>& is_terminal
        );
        friend f_partial_matches_vec drake::symbolic::VisitFormula<f_partial_matches_vec>(
            PatternMatchingTrie*, const Formula& e,
            FormNode& parent, const std::shared_ptr<substitutions_map>& substitutions, f_matches_vec& matches
        );
        friend FormNode& drake::symbolic::VisitFormula<FormNode&>(
            PatternMatchingTrie*, const Formula& e, FormNode& parent,
            const std::optional<Expression>& is_terminal
        );
    };
} // namespace dreal


#endif //pattern_matching_trie_H
