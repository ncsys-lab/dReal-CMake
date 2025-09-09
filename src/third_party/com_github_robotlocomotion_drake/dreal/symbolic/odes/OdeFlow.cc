//
// Created by Kunal Sheth on 8/24/25.
//


#include "dreal/symbolic/odes/OdeFlow.h"

#include <utility>

namespace dreal
{
    OdeFlow::OdeFlow(std::string  flow_id, const std::vector<std::pair<Variable, Expression>>& ode_list) :
        name(std::move(flow_id)),
        ode_list(ode_list) {
        for (const auto& [ode_var,ode_rhs] : ode_list) {
            if (is_constant(ode_rhs, 0)) ode_pars.insert(ode_var);
            else ode_vars.insert(ode_var);
        }
    }
}
