//
// Created by Kunal Sheth on 9/10/25.
//

#ifndef DREAL4_CMAKE_SYMBOLIC_ODES_CELL_H
#define DREAL4_CMAKE_SYMBOLIC_ODES_CELL_H

#include "dreal/symbolic/symbolic_formula_cell.h"

namespace dreal::drake::symbolic
{
    class FormulaForallT : public FormulaCell
    {
    public:
        FormulaForallT(const std::shared_ptr<const OdeFlow>&, const Expression& lb, const Expression& ub, const Formula& bound_f);
        bool EqualTo(const FormulaCell& f) const override;
        bool Less(const FormulaCell& f) const override;
        bool Evaluate(const Environment& env) const override;
        Formula Substitute(const ExpressionSubstitution& expr_subst,
                           const FormulaSubstitution& formula_subst) override;
        std::ostream& Display(std::ostream& os) const override;

        const std::shared_ptr<const OdeFlow>& get_flow() const { return flow_; }
        const Expression& get_lb() const { return lb_; }
        const Expression& get_ub() const { return ub_; }
        const Formula& get_bound_f() const { return bound_f_; }
        const Variables& get_bound_vars() const { return bound_f_.GetFreeVariables(); }

    private:
        const std::shared_ptr<const OdeFlow> flow_; // flow number
        const Expression lb_;
        const Expression ub_;
        const Formula bound_f_;
    };

    class FormulaIntegral : public FormulaCell
    {
    public:
        FormulaIntegral(
            const Expression& time_0, const Expression& time_t,
            std::vector<Expression> vec_0, std::vector<Expression> vec_t,
            const std::shared_ptr<const OdeFlow>
            &);
        bool EqualTo(const FormulaCell& e) const override;
        bool Less(const FormulaCell& e) const override;
        bool Evaluate(const Environment& env) const override;
        Formula Substitute(const ExpressionSubstitution& expr_subst,
                           const FormulaSubstitution& formula_subst) override;
        std::ostream& Display(std::ostream& os) const override;

        const Expression& get_time_0() const { return time_0_; };
        const Expression& get_time_t() const { return time_t_; };
        const std::vector<Expression>& get_vec_0() const { return vec_0_; };
        const std::vector<Expression>& get_vec_t() const { return vec_t_; };

        const std::vector<Variable>& get_vars_0() const { return vars_0_; };
        const std::vector<Variable>& get_vars_t() const { return vars_t_; };
        const std::vector<Variable>& get_pars_0() const { return pars_0_; };
        const std::vector<Variable>& get_pars_t() const { return pars_t_; };

        const std::shared_ptr<const OdeFlow>& get_flow() const { return flow_; };

    private:
        const Expression time_0_;
        const Expression time_t_;
        const std::vector<Expression> vec_0_;
        const std::vector<Expression> vec_t_;

        std::vector<Variable> vars_0_;
        std::vector<Variable> vars_t_;
        std::vector<Variable> pars_0_;
        std::vector<Variable> pars_t_;

        const std::shared_ptr<const OdeFlow> flow_;
    };

    bool is_forallT(const FormulaCell& f);
    const FormulaForallT* to_forallT(const FormulaCell* f_ptr);
    const FormulaForallT* to_forallT(const Formula& f);

    bool is_integral(const FormulaCell& c);
    const FormulaIntegral* to_integral(const FormulaCell* expr_ptr);
    const FormulaIntegral* to_integral(const Formula& e);
}

#endif //DREAL4_CMAKE_SYMBOLIC_ODES_CELL_H
