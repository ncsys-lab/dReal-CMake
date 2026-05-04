#pragma once
// C++17-compatible interface for the Codac v2 ODE integration backend.
// The implementation (contractor_odes_codac.cc) is compiled with C++20.

#include <utility>
#include <vector>

#include "dreal/symbolic/symbolic.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"

namespace dreal
{
    // Result type returned by run_lohner_integration.
    struct CodacOdeResult {
        // Element i: narrowed [lb, ub] for m_vars_t[i] (terminal state).
        std::vector<std::pair<double, double>> vars_t_narrowed;
        // Element i: narrowed [lb, ub] for m_vars_0[i] (initial state).
        // Populated when CtcLohner FWD_BWD is used (backward pass narrows the source).
        std::vector<std::pair<double, double>> vars_0_narrowed;
        double t_new_lb{0.0};
        double t_new_ub{-1.0};
        bool found{false};  // true if the tube is non-empty after integration
    };

    // One enclosure snapshot at a single integration step.
    struct CodacTracePoint {
        double t_lb;
        double t_ub;
        std::vector<std::pair<double, double>> var_enclosures;  // one per ODE state var
    };

    // Result type returned by run_lohner_trace.
    struct CodacTraceResult {
        std::vector<CodacTracePoint> points;
        bool succeeded{false};
    };

    // Run Lohner ODE integration using Codac v2.
    //
    // ode_state_vars[i] = ODE state variable whose initial value is u0[i] and
    //                     whose terminal value is X_t[i].
    // u0[i]   = (lb, ub) of the start-of-integration interval for variable i
    // X_t[i]  = (lb, ub) of the target state interval for variable i
    // t_ub    = upper bound of the time variable
    // forward = true for FWD integration, false for BWD
    // n_steps = number of Lohner integration steps (default 200)
    //
    // Returns CodacOdeResult::found == false if translation fails or no
    // trajectory intersects X_t.
    CodacOdeResult run_lohner_integration(
        const OdeFlow& flow,
        const std::vector<Variable>& ode_state_vars,
        const std::vector<std::pair<double, double>>& u0,
        const std::vector<std::pair<double, double>>& X_t,
        double t_ub,
        bool forward,
        int n_steps = 20);

    // Run Lohner ODE integration and collect all enclosures for trace generation.
    //
    // Unlike run_lohner_integration, this does NOT filter by a target state —
    // it returns an enclosure for every integration step so callers can build
    // a full trajectory visualization.
    //
    // u0[i]   = (lb, ub) of the start-of-integration interval for variable i
    // t_ub    = upper bound of the time variable
    // forward = true for FWD integration, false for BWD
    // n_steps = number of Lohner integration steps (default 200)
    CodacTraceResult run_lohner_trace(
        const OdeFlow& flow,
        const std::vector<Variable>& ode_state_vars,
        const std::vector<std::pair<double, double>>& u0,
        double t_ub,
        bool forward,
        int n_steps = 200);

} // namespace dreal
