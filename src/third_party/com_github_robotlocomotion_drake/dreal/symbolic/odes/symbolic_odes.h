#pragma once

#include "OdeFlow.h"

namespace dreal::drake::symbolic
{
    Formula forallT(const std::shared_ptr<const OdeFlow>& flow, const Expression& lb, const Expression& ub, const Formula& f);
    bool is_forallT(const Formula& f);

    Formula integral(const Expression& time_0, const Expression& time_t,
                     const std::vector<Expression>& vec_0, const std::vector<Expression>& vec_t,
                     const std::shared_ptr<const OdeFlow>& flow);
    bool is_integral(const Formula& e);
} // namespace dreal::drake::symbolic
