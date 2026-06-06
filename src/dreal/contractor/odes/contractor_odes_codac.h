#pragma once
// C++17-compatible interface for the Codac v2 ODE integration backend.
// The implementation (contractor_odes_codac.cc) is compiled with C++20.

#include <memory>
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

    // Opaque handle wrapping a pre-built Codac AnalyticFunction and CtcLohner.
    // The full type is only visible to the C++20 codac translation unit.
    //
    // Build one with make_codac_ode_cache() per ODE flow and reuse it across
    // every Prune() call for that flow — avoids re-translating the symbolic
    // RHS into Codac's expression tree on every call (which dominates per-call
    // cost on ODE-heavy benchmarks).
    class CodacOdeCache;

    // Build a cache for `flow`. Returns nullptr if expression translation fails.
    // `ordered_vars` MUST match the order in `flow.ode_list` (one entry per ODE
    // state variable).
    //
    // The cache holds a reference to `flow` (the shared_ptr is stored alongside
    // the cache entry) to guarantee `flow.get()` stays valid for the lifetime
    // of the cache. Without this, the internal `flow_cache_map` (keyed by raw
    // pointer) could return stale entries when an OdeFlow is destroyed and a
    // new one is allocated at the same address.
    std::shared_ptr<CodacOdeCache> make_codac_ode_cache(
        std::shared_ptr<const OdeFlow> flow,
        const std::vector<Variable>& ordered_vars);

    // True if every RHS in the cached flow is the literal 0 constant — i.e.
    // every state variable is constant along the trajectory. Callers can use
    // this to short-circuit CtcLohner and just intersect X_0 with X_t.
    bool codac_ode_cache_is_trivial(const std::shared_ptr<CodacOdeCache>& cache);

    // Run Lohner ODE integration using a pre-built cache.
    //
    // u0[i]        = (lb, ub) of the start-of-integration interval for variable i
    // X_t[i]       = (lb, ub) of the target state interval for variable i
    // t_ub         = upper bound of the time variable
    // forward      = ignored; CtcLohner FWD_BWD narrows both endpoints jointly
    // n_steps_hint = lower bound on Lohner integration steps. The actual
    //                step count is chosen adaptively as
    //                  clamp(max(n_steps_hint, ceil(t_ub * 2)), n_steps_hint, 60)
    //                so short horizons stay at the hint (default 20) and
    //                long horizons get h ≤ 0.5.
    //
    // Returns CodacOdeResult::found == false if cache is null or no trajectory
    // intersects X_t.
    CodacOdeResult run_lohner_integration(
        const std::shared_ptr<CodacOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0,
        const std::vector<std::pair<double, double>>& X_t,
        double t_ub,
        bool forward,
        int n_steps_hint = 20);

    // Run Lohner ODE integration and collect all enclosures for trace generation.
    //
    // Unlike run_lohner_integration, this does NOT filter by a target state —
    // it returns an enclosure for every integration step so callers can build
    // a full trajectory visualization.
    CodacTraceResult run_lohner_trace(
        const std::shared_ptr<CodacOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0,
        double t_ub,
        bool forward,
        int n_steps = 200);

    // Run a single backward Lohner integration from u0 = X_t at real time t_ub
    // to real time 0, using LohnerAlgorithm(forward=false). Returns
    // vars_t_narrowed = an over-approximation of the backward image of u0
    // (i.e. the set of states at real time 0 whose forward trajectory under
    // dx/dt = f(x) reaches u0 at real time t_ub). The caller is expected to
    // intersect this with the current X_0 bounds.
    //
    // vars_0_narrowed is left empty — this routine provides one-way
    // narrowing only. Use the FWD contractor's run_lohner_integration if
    // joint narrowing of both endpoints is needed.
    //
    // n_steps_hint follows the same adaptive policy as run_lohner_integration:
    //   clamp(max(n_steps_hint, ceil(t_ub * 2)), n_steps_hint, 60)
    CodacOdeResult run_lohner_bwd_oneshot(
        const std::shared_ptr<CodacOdeCache>& cache,
        const std::vector<std::pair<double, double>>& Xt_bounds,
        double t_ub,
        int n_steps_hint = 20);

} // namespace dreal
