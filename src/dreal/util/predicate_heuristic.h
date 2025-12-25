//
// Created by Kunal Sheth on 2/19/25.
//

#ifndef predicate_heuristic_H
#define predicate_heuristic_H

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal
{
    class PredicateHeuristic
    {
    // todo: unit test this!!!

    public:
        typedef struct
        {
            double raw_score;
            unsigned unique_variables;
            unsigned variable_cntr;
            unsigned constant_cntr;
            unsigned realconstant_cntr;
            unsigned addition_cntr;
            unsigned multiplication_cntr;
            unsigned division_cntr;
            unsigned log_cntr;
            unsigned abs_cntr;
            unsigned exp_cntr;
            unsigned sqrt_cntr;
            unsigned pow_cntr;
            unsigned trigonometry_cntr;
            unsigned min_cntr;
            unsigned max_cntr;
            unsigned ifthenelse_cntr;
            unsigned uninterpretedfunction_cntr;
            unsigned false_cntr;
            unsigned true_cntr;
            unsigned equalto_cntr;
            unsigned notequalto_cntr;
            unsigned inequality_cntr;
            unsigned conjunction_cntr;
            unsigned disjunction_cntr;
            unsigned negation_cntr;
            unsigned forall_cntr;
            unsigned forallt_cntr;
            unsigned integral_cntr;
        } predicate_stats_t;

        static std::string predicate_stats_csv_header(const std::string& prefix) {
            std::ostringstream s;
            s << prefix << "raw_score,";
            s << prefix << "unique_variables,";
            s << prefix << "variable_cntr,";
            s << prefix << "constant_cntr,";
            s << prefix << "realconstant_cntr,";
            s << prefix << "addition_cntr,";
            s << prefix << "multiplication_cntr,";
            s << prefix << "division_cntr,";
            s << prefix << "log_cntr,";
            s << prefix << "abs_cntr,";
            s << prefix << "exp_cntr,";
            s << prefix << "sqrt_cntr,";
            s << prefix << "pow_cntr,";
            s << prefix << "trigonometry_cntr,";
            s << prefix << "min_cntr,";
            s << prefix << "max_cntr,";
            s << prefix << "ifthenelse_cntr,";
            s << prefix << "uninterpretedfunction_cntr,";
            s << prefix << "false_cntr,";
            s << prefix << "true_cntr,";
            s << prefix << "equalto_cntr,";
            s << prefix << "notequalto_cntr,";
            s << prefix << "inequality_cntr,";
            s << prefix << "conjunction_cntr,";
            s << prefix << "disjunction_cntr,";
            s << prefix << "negation_cntr,";
            s << prefix << "forall_cntr";
            s << prefix << "forallt_cntr";
            s << prefix << "integral_cntr";
            return s.str();
        }

        predicate_stats_t collect_statistics(const Expression& e);
        predicate_stats_t collect_statistics(const Formula& f, bool inverted = false);

    private:
        // must be reference to const bool because of `drake::symbolic::VisitExpression...` template requires it.
#define ADD_DECL(name) void name (const Expression &e, predicate_stats_t &stats) const
        ADD_DECL(VisitVariable);
        ADD_DECL(VisitConstant);
        ADD_DECL(VisitRealConstant);
        ADD_DECL(VisitAddition);
        ADD_DECL(VisitMultiplication);
        ADD_DECL(VisitDivision);
        ADD_DECL(VisitLog);
        ADD_DECL(VisitAbs);
        ADD_DECL(VisitExp);
        ADD_DECL(VisitSqrt);
        ADD_DECL(VisitPow);
        ADD_DECL(VisitSin);
        ADD_DECL(VisitCos);
        ADD_DECL(VisitTan);
        ADD_DECL(VisitAsin);
        ADD_DECL(VisitAcos);
        ADD_DECL(VisitAtan);
        ADD_DECL(VisitAtan2);
        ADD_DECL(VisitSinh);
        ADD_DECL(VisitCosh);
        ADD_DECL(VisitTanh);
        ADD_DECL(VisitMin);
        ADD_DECL(VisitMax);
        ADD_DECL(VisitIfThenElse);
        ADD_DECL(VisitUninterpretedFunction);
#undef ADD_DECL

        // must be reference to const bool because of `drake::symbolic::VisitExpression...` template requires it.
#define ADD_DECL(name) void name (const Formula &e, predicate_stats_t &stats, const bool &inverted) const
        ADD_DECL(VisitFalse);
        ADD_DECL(VisitTrue);
        ADD_DECL(VisitVariable);
        ADD_DECL(VisitEqualTo);
        ADD_DECL(VisitNotEqualTo);
        ADD_DECL(VisitGreaterThan);
        ADD_DECL(VisitGreaterThanOrEqualTo);
        ADD_DECL(VisitLessThan);
        ADD_DECL(VisitLessThanOrEqualTo);
        ADD_DECL(VisitConjunction);
        ADD_DECL(VisitDisjunction);
        ADD_DECL(VisitNegation);
        ADD_DECL(VisitForall);
        ADD_DECL(VisitForallT);
        ADD_DECL(VisitIntegral);
#undef VISIT_DECL
#undef ADD_DECL
#undef VISIT_AND_ADD_DECL

        std::unordered_map<Formula, predicate_stats_t> fcache_pos;
        std::unordered_map<Formula, predicate_stats_t> fcache_neg;
        std::unordered_map<Expression, predicate_stats_t> ecache;

        void recHeurExprCheckCache(const Expression& e, predicate_stats_t& stats) const;
        void recHeurExpr(const Expression& e, predicate_stats_t& stats) const;

        void recHeurFormCheckCache(const Formula& f, predicate_stats_t& stats, const bool& inverted) const;
        void recHeurForm(const Formula& f, predicate_stats_t& stats, const bool& inverted) const;

        void UnaryOpHelper(const Expression& e, predicate_stats_t& stats) const;
        void UnaryOpHelper(const Formula& f, predicate_stats_t& stats, const bool& inverted) const;
        void BinaryOpHelper(const Expression& e, predicate_stats_t& stats) const;
        void BinaryOpHelper(const Formula& f, predicate_stats_t& stats, const bool& inverted) const;
        void NaryOpHelper(const Formula& f, predicate_stats_t& stats, bool inverted) const;

        friend void drake::symbolic::VisitExpression<void>(
            const PredicateHeuristic*, const Expression&, predicate_stats_t&
        );
        friend void drake::symbolic::VisitFormula<void>(
            const PredicateHeuristic*, const Formula&, predicate_stats_t&, const bool&
        );
    };

    inline std::ostream& operator<<(std::ostream& os, const PredicateHeuristic::predicate_stats_t& stats) {
        os << stats.raw_score << ',';
        os << stats.unique_variables << ',';
        os << stats.variable_cntr << ',';
        os << stats.constant_cntr << ',';
        os << stats.realconstant_cntr << ',';
        os << stats.addition_cntr << ',';
        os << stats.multiplication_cntr << ',';
        os << stats.division_cntr << ',';
        os << stats.log_cntr << ',';
        os << stats.abs_cntr << ',';
        os << stats.exp_cntr << ',';
        os << stats.sqrt_cntr << ',';
        os << stats.pow_cntr << ',';
        os << stats.trigonometry_cntr << ',';
        os << stats.min_cntr << ',';
        os << stats.max_cntr << ',';
        os << stats.ifthenelse_cntr << ',';
        os << stats.uninterpretedfunction_cntr << ',';
        os << stats.false_cntr << ',';
        os << stats.true_cntr << ',';
        os << stats.equalto_cntr << ',';
        os << stats.notequalto_cntr << ',';
        os << stats.inequality_cntr << ',';
        os << stats.conjunction_cntr << ',';
        os << stats.disjunction_cntr << ',';
        os << stats.negation_cntr << ',';
        os << stats.forall_cntr << ',';
        os << stats.forallt_cntr << ',';
        os << stats.integral_cntr;
        return os;
    }

    inline PredicateHeuristic::predicate_stats_t& operator+=(
        PredicateHeuristic::predicate_stats_t& lhs,
        const PredicateHeuristic::predicate_stats_t& rhs
    ) {
        lhs.raw_score += rhs.raw_score;
        lhs.unique_variables += rhs.unique_variables;
        lhs.variable_cntr += rhs.variable_cntr;
        lhs.constant_cntr += rhs.constant_cntr;
        lhs.realconstant_cntr += rhs.realconstant_cntr;
        lhs.addition_cntr += rhs.addition_cntr;
        lhs.multiplication_cntr += rhs.multiplication_cntr;
        lhs.division_cntr += rhs.division_cntr;
        lhs.log_cntr += rhs.log_cntr;
        lhs.abs_cntr += rhs.abs_cntr;
        lhs.exp_cntr += rhs.exp_cntr;
        lhs.sqrt_cntr += rhs.sqrt_cntr;
        lhs.pow_cntr += rhs.pow_cntr;
        lhs.trigonometry_cntr += rhs.trigonometry_cntr;
        lhs.min_cntr += rhs.min_cntr;
        lhs.max_cntr += rhs.max_cntr;
        lhs.ifthenelse_cntr += rhs.ifthenelse_cntr;
        lhs.uninterpretedfunction_cntr += rhs.uninterpretedfunction_cntr;
        lhs.false_cntr += rhs.false_cntr;
        lhs.true_cntr += rhs.true_cntr;
        lhs.equalto_cntr += rhs.equalto_cntr;
        lhs.notequalto_cntr += rhs.notequalto_cntr;
        lhs.inequality_cntr += rhs.inequality_cntr;
        lhs.conjunction_cntr += rhs.conjunction_cntr;
        lhs.disjunction_cntr += rhs.disjunction_cntr;
        lhs.negation_cntr += rhs.negation_cntr;
        lhs.forall_cntr += rhs.forall_cntr;
        lhs.forallt_cntr += rhs.forallt_cntr;
        lhs.integral_cntr += rhs.integral_cntr;
        return lhs;
    }
} // namespace dreal

#endif //predicate_heuristic_H
