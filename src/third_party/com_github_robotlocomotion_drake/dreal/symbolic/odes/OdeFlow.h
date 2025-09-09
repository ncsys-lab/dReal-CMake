//
// Created by Kunal Sheth on 8/24/25.
//

#ifndef DREAL4_CMAKE_ODEFLOW_H
#define DREAL4_CMAKE_ODEFLOW_H

#include <string>
#include "dreal/symbolic/symbolic.h"

namespace dreal::drake::symbolic
{
    class OdeFlow
    {
    public:
        const std::string name;
        const std::vector<std::pair<Variable, Expression>> ode_list;

        OdeFlow(std::string flow_id, const std::vector<std::pair<Variable, Expression>>& ode_list);

        const std::set<Variable>& get_ode_vars() const { return ode_vars; }
        const std::set<Variable>& get_ode_pars() const { return ode_pars; }
        bool is_var(const Variable& v) const { return ode_vars.count(v) > 0; }
        bool is_par(const Variable& v) const { return ode_pars.count(v) > 0; }

        friend bool operator==(const OdeFlow& lhs, const OdeFlow& rhs) {
            if (&lhs == &rhs) { return true; } // short-circuit: same object
            return lhs.name == rhs.name /*&& std::equal(
                lhs.ode_list.begin(), lhs.ode_list.end(), rhs.ode_list.begin(), rhs.ode_list.end(),
                [](const auto& lvar_exp, const auto& rvar_exp) {
                    return lvar_exp.first.equal_to(rvar_exp.first) && lvar_exp.second.EqualTo(rvar_exp.second);
                }
            )*/;
        }

    private:
        std::set<Variable> ode_vars;
        std::set<Variable> ode_pars;
    };
}

#endif //DREAL4_CMAKE_ODEFLOW_H
