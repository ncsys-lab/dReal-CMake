//
// Created by Kunal Sheth on 8/24/25.
//


#include "dreal/symbolic/odes/OdeFlow.h"

#include <utility>

namespace dreal::drake::symbolic
{
    OdeFlow::OdeFlow(std::string flow_id, const std::vector<std::pair<Variable, Expression>>& ode_list) :
        name(std::move(flow_id)),
        ode_list(ode_list) {
        for (const auto& [ode_var,ode_rhs] : ode_list) {
            if (is_constant(ode_rhs, 0)) ode_pars.insert(ode_var);
            else ode_vars.insert(ode_var);
        }
    }

    bool operator==(const OdeFlow& lhs, const OdeFlow& rhs) {
        if (&lhs == &rhs) { return true; } // short-circuit: same object
        return lhs.name == rhs.name /*&& std::equal( // implements length check under the hood.
                lhs.ode_list.begin(), lhs.ode_list.end(), rhs.ode_list.begin(), rhs.ode_list.end(),
                [](const auto& lvar_exp, const auto& rvar_exp) {
                    return lvar_exp.first.equal_to(rvar_exp.first) && lvar_exp.second.EqualTo(rvar_exp.second);
                }
            )*/;
    }
}
