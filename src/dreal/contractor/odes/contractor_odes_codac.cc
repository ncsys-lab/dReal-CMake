// This file MUST be compiled with C++20 (Codac v2 requires concepts, std::numbers).
// CMakeLists.txt sets COMPILE_OPTIONS -std=c++20 for this file only.

#include "contractor_odes_codac.h"

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <codac2_CtcLohner.h>
#include <codac2_AnalyticFunction.h>
#include <codac2_analytic_variables.h>
#include <codac2_analytic_constants.h>
#include <codac2_operators.h>
#include <codac2_vec.h>

// dReal symbolic types (C++17, but compatible with C++20)
#include "dreal/symbolic/symbolic.h"

namespace dreal
{
    // -------------------------------------------------------------------------
    // Expression translator: dReal Expression → codac2::ScalarExpr
    // -------------------------------------------------------------------------

    static codac2::ScalarExpr translate_expr(
        const Expression& e,
        const std::unordered_map<Variable::Id, int>& idx_map,
        const codac2::VectorVar& x_var)
    {
        switch (e.get_kind()) {
        case ExpressionKind::Constant:
        case ExpressionKind::RealConstant:
            return codac2::const_value(get_constant_value(e));

        case ExpressionKind::Var: {
            const Variable v = get_variable(e);
            return x_var[idx_map.at(v.get_id())];
        }

        case ExpressionKind::Add: {
            auto result = codac2::const_value(get_constant_in_addition(e));
            for (const auto& [term, coeff] : get_expr_to_coeff_map_in_addition(e))
                result = result + codac2::const_value(coeff) * translate_expr(term, idx_map, x_var);
            return result;
        }

        case ExpressionKind::Mul: {
            auto result = codac2::const_value(get_constant_in_multiplication(e));
            for (const auto& [base, exp_e] : get_base_to_exponent_map_in_multiplication(e))
                result = result * codac2::pow(translate_expr(base, idx_map, x_var),
                                              translate_expr(exp_e, idx_map, x_var));
            return result;
        }

        case ExpressionKind::Div:
            return translate_expr(get_first_argument(e),  idx_map, x_var) /
                   translate_expr(get_second_argument(e), idx_map, x_var);

        case ExpressionKind::Pow:
            return codac2::pow(translate_expr(get_first_argument(e),  idx_map, x_var),
                               translate_expr(get_second_argument(e), idx_map, x_var));

        case ExpressionKind::Exp:
            return codac2::exp(translate_expr(get_argument(e), idx_map, x_var));

        case ExpressionKind::Sqrt:
            return codac2::sqrt(translate_expr(get_argument(e), idx_map, x_var));

        case ExpressionKind::Sin:
            return codac2::sin(translate_expr(get_argument(e), idx_map, x_var));

        case ExpressionKind::Cos:
            return codac2::cos(translate_expr(get_argument(e), idx_map, x_var));

        case ExpressionKind::Tan:
            return codac2::tan(translate_expr(get_argument(e), idx_map, x_var));

        case ExpressionKind::Abs:
            return codac2::abs(translate_expr(get_argument(e), idx_map, x_var));

        default:
            throw std::runtime_error(
                "translate_expr: unsupported ExpressionKind " +
                std::to_string(static_cast<int>(e.get_kind())));
        }
    }

    // -------------------------------------------------------------------------
    // Build codac2::AnalyticFunction<VectorType> from OdeFlow
    // -------------------------------------------------------------------------

    static std::optional<codac2::AnalyticFunction<codac2::VectorType>>
    build_ode_fn(const OdeFlow& flow, const std::vector<Variable>& ordered_vars)
    {
        const int n = static_cast<int>(ordered_vars.size());

        std::unordered_map<Variable::Id, int> idx_map;
        for (int i = 0; i < n; ++i)
            idx_map[ordered_vars[i].get_id()] = i;

        std::unordered_map<Variable::Id, Expression> rhs_map;
        for (const auto& [var, rhs] : flow.ode_list)
            rhs_map[var.get_id()] = rhs;

        codac2::VectorVar x_var(n);
        try {
            std::vector<codac2::ScalarExpr> rhs_exprs;
            rhs_exprs.reserve(static_cast<std::size_t>(n));
            for (const auto& v : ordered_vars)
                rhs_exprs.push_back(translate_expr(rhs_map.at(v.get_id()), idx_map, x_var));
            return codac2::AnalyticFunction<codac2::VectorType>({x_var}, codac2::vec(rhs_exprs));
        } catch (...) {
            return std::nullopt;
        }
    }

    // -------------------------------------------------------------------------
    // Public entry point
    // -------------------------------------------------------------------------

    CodacOdeResult run_lohner_integration(
        const OdeFlow& flow,
        const std::vector<Variable>& ode_state_vars,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& X_t_bounds,
        double t_ub,
        bool forward,
        int n_steps)
    {
        CodacOdeResult result;
        const int n = static_cast<int>(ode_state_vars.size());
        if (n == 0 || t_ub <= 0.0 || n_steps <= 0) return result;

        auto ode_fn_opt = build_ode_fn(flow, ode_state_vars);
        if (!ode_fn_opt) return result;

        const double h = t_ub / n_steps;
        if (h <= 0.0) return result;

        // Build initial condition
        codac2::IntervalVector u0(n);
        for (int i = 0; i < n; ++i)
            u0[i] = codac2::Interval(u0_bounds[static_cast<std::size_t>(i)].first,
                                     u0_bounds[static_cast<std::size_t>(i)].second);

        // Build target state
        codac2::IntervalVector X_t(n);
        for (int i = 0; i < n; ++i)
            X_t[i] = codac2::Interval(X_t_bounds[static_cast<std::size_t>(i)].first,
                                      X_t_bounds[static_cast<std::size_t>(i)].second);

        try {
            codac2::LohnerAlgorithm algo(&(*ode_fn_opt), h, forward, u0);

            double t_new_lb = std::numeric_limits<double>::infinity();
            double t_new_ub = -1.0;
            std::vector<codac2::Interval> hull(static_cast<std::size_t>(n));
            bool found = false;

            for (int k = 1; k <= n_steps; ++k) {
                algo.integrate(1);
                const codac2::IntervalVector& u_k = algo.getLocalEnclosure();
                const double t_k = k * h;

                bool ok = true;
                for (int i = 0; i < n && ok; ++i)
                    if ((u_k[i] & X_t[i]).is_empty()) ok = false;

                if (ok) {
                    if (!found) {
                        for (int i = 0; i < n; ++i) hull[static_cast<std::size_t>(i)] = u_k[i];
                        t_new_lb = t_k - h;
                        t_new_ub = t_k;
                        found = true;
                    } else {
                        for (int i = 0; i < n; ++i) hull[static_cast<std::size_t>(i)] |= u_k[i];
                        t_new_lb = std::min(t_new_lb, t_k - h);
                        t_new_ub = std::max(t_new_ub, t_k);
                    }
                }
            }

            if (found) {
                result.found = true;
                result.t_new_lb = t_new_lb;
                result.t_new_ub = t_new_ub;
                result.vars_t_narrowed.reserve(static_cast<std::size_t>(n));
                for (int i = 0; i < n; ++i)
                    result.vars_t_narrowed.emplace_back(
                        hull[static_cast<std::size_t>(i)].lb(),
                        hull[static_cast<std::size_t>(i)].ub());
            }
        } catch (const codac2::GlobalEnclosureError&) {
            // Integration failed; return empty result
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // Trace: collect all enclosures for visualization
    // -------------------------------------------------------------------------

    CodacTraceResult run_lohner_trace(
        const OdeFlow& flow,
        const std::vector<Variable>& ode_state_vars,
        const std::vector<std::pair<double, double>>& u0_bounds,
        double t_ub,
        bool forward,
        int n_steps)
    {
        CodacTraceResult result;
        const int n = static_cast<int>(ode_state_vars.size());
        if (n == 0 || t_ub <= 0.0 || n_steps <= 0) return result;

        auto ode_fn_opt = build_ode_fn(flow, ode_state_vars);
        if (!ode_fn_opt) return result;

        const double h = t_ub / n_steps;
        if (h <= 0.0) return result;

        codac2::IntervalVector u0(n);
        for (int i = 0; i < n; ++i)
            u0[i] = codac2::Interval(u0_bounds[static_cast<std::size_t>(i)].first,
                                     u0_bounds[static_cast<std::size_t>(i)].second);

        try {
            codac2::LohnerAlgorithm algo(&(*ode_fn_opt), h, forward, u0);

            result.points.reserve(static_cast<std::size_t>(n_steps));
            for (int k = 1; k <= n_steps; ++k) {
                algo.integrate(1);
                const codac2::IntervalVector& u_k = algo.getLocalEnclosure();
                const double t_k = k * h;

                CodacTracePoint pt;
                pt.t_lb = t_k - h;
                pt.t_ub = t_k;
                pt.var_enclosures.reserve(static_cast<std::size_t>(n));
                for (int i = 0; i < n; ++i)
                    pt.var_enclosures.emplace_back(u_k[i].lb(), u_k[i].ub());
                result.points.push_back(std::move(pt));
            }
            result.succeeded = true;
        } catch (const codac2::GlobalEnclosureError&) {
            // Return whatever points were collected before the failure.
            result.succeeded = !result.points.empty();
        }

        return result;
    }

} // namespace dreal
