#pragma once
// C++17 interface for the CAPD v6 ODE integration backend.
// The implementation (contractor_odes_capd.cc) is also compiled at C++17.
//
// CAPD's Taylor order-10 integrator is the sole ODE backend for dReal after
// the Codac elimination — see ../../../../CODAC_MIGRATION.md.

#include <memory>
#include <utility>
#include <vector>

#include "dreal/symbolic/symbolic.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"
#include "dreal/util/rounding.h"

namespace dreal
{
    // Result type returned by the integration entry points.
    struct CapdOdeResult {
        // Element i: narrowed [lb, ub] for m_vars_t[i] (terminal state).
        std::vector<std::pair<double, double>> vars_t_narrowed;
        // Element i: narrowed [lb, ub] for m_vars_0[i] (initial state).
        // Populated when run_capd_fwd performed both a forward integration
        // (narrowing X_t) and a backward integration from the narrowed X_t
        // (narrowing X_0).
        std::vector<std::pair<double, double>> vars_0_narrowed;
        double t_new_lb{0.0};
        double t_new_ub{-1.0};
        bool found{false};
    };

    // Opaque cache holding the per-flow capd::IMap (and the negated -f(x)
    // IMap used for backward integration). The full type is only visible to
    // the CAPD translation unit so the rest of the codebase doesn't pull in
    // CAPD headers.
    //
    // Same lifetime guarantee as CodacOdeCache: built once per OdeFlow,
    // reused across every Prune call. capd::IOdeSolver instances are
    // constructed per-call inside run_capd_* because they carry mutable
    // step-size state and can't be safely shared across parallel ICP
    // workers (unlike Codac's const CtcLohner::contract).
    class CapdOdeCache;

    // Build a cache for `flow`. Returns nullptr only when `flow` is null;
    // it RAISES (std::runtime_error) if an RHS cannot be translated into the
    // capd::IMap string format (e.g. an unsupported ExpressionKind).
    //
    // `ordered_vars` MUST match the order in `flow.ode_list`.
    //
    // The cache holds a shared_ptr<const OdeFlow> in the slot to keep the
    // OdeFlow alive while the cache entry is live — same fix as the Codac
    // cache's pointer-reuse hazard (see contractor_odes_codac.cc).
    std::shared_ptr<CapdOdeCache> make_capd_ode_cache(
        std::shared_ptr<const OdeFlow> flow,
        const std::vector<Variable>& ordered_vars);

    // True if every RHS in the cached flow is the literal 0 constant. The
    // Codac cache also detects this; either flag is sufficient for the
    // trivial-flow short-circuit in contractor_odes.cc, but exposing it on
    // the CAPD side keeps the contractor self-contained.
    bool capd_ode_cache_is_trivial(const std::shared_ptr<CapdOdeCache>& cache);

    // Run CAPD forward integration via Taylor order-10.
    //
    // Forward step: integrate the cached f(x) IMap from u0_bounds = X_0 over
    // [0, t_ub] using capd::IOdeSolver(order=kCapdTaylorOrder, i.e. 10) + capd::ITimeMap. Intersect
    // the terminal enclosure with X_t_bounds → vars_t_narrowed.
    //
    // Backward step (free with the forward solver — see implementation):
    // integrate the cached -f(x) IMap from vars_t_narrowed over [0, t_ub]
    // → vars_0_narrowed. This recovers the joint narrowing pattern that
    // Codac's CtcLohner FWD_BWD does in one call.
    //
    // n_steps_hint is a lower bound; CAPD chooses its actual step size
    // adaptively from `order` and target tolerance internally. We use it
    // only to scope the ITimeMap's max-step parameter so the time horizon
    // is reachable without endless step adaptation.
    //
    // Returns CapdOdeResult::found == false if cache is null, the
    // integrator diverges, or no trajectory intersects X_t.
    //
    // par_bounds carries the current interval value of each flow parameter
    // (a flow variable whose d/dt is the literal 0), ordered to match the
    // cache's parameter list (== ode_list parameter order == m_pars_0 order).
    // These are bound into a private copy of the cached IMap via
    // setParameter before integration; they are NOT integration variables.
    CapdOdeResult run_capd_fwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& X_t_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        const NearestRounding& nr,
        int n_steps_hint = 20);

    // Run CAPD one-shot backward integration.
    //
    // Integrates the cached -f(x) IMap from u0 = Xt_bounds (in the caller's
    // swapped frame, this is original X_t) over [0, t_ub]. The terminal
    // enclosure is the backward image — the set of states at real time 0
    // whose forward trajectory under f(x) reaches X_t. Caller intersects
    // this with the current m_vars_t bounds (= original X_0).
    //
    // vars_0_narrowed is left empty — one-way narrowing only. Mirrors
    // Codac's run_lohner_bwd_oneshot.
    //
    // Soundness: for any state x_0 reaching some point of X_t under forward
    // dynamics, x_0 must lie in the backward image. Removing points outside
    // it is sound by construction.
    CapdOdeResult run_capd_bwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& Xt_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        const NearestRounding& nr,
        int n_steps_hint = 20);

    // -------------------------------------------------------------------------
    // Trace generation (for `dreal4 --visualize`).
    // -------------------------------------------------------------------------

    // One slice of the integrated trajectory. The slice spans real time
    // [t_lb, t_ub] and var_enclosures[i] is the over-approximating box of
    // state variable i over that slice.
    struct CapdTracePoint {
        double t_lb;
        double t_ub;
        std::vector<std::pair<double, double>> var_enclosures;
    };

    struct CapdTraceResult {
        std::vector<CapdTracePoint> points;
        bool succeeded{false};
    };

    // Integrate the cached flow from `u0` over [0, t_ub] and record an
    // enclosure for each of n_steps equally-spaced sub-slices. Mirrors the
    // JSON shape of the now-deleted Codac run_lohner_trace() so the
    // visualizer's input contract is preserved.
    //
    // forward = true  → integrate f(x)  (cache->fn_fwd)
    // forward = false → integrate -f(x) (cache->fn_bwd) for reverse-time view
    //
    // Returns succeeded=false if the cache is null or the integrator diverges
    // before reaching t_ub. The points actually recorded up to the divergence
    // are still populated so partial traces remain visualizable.
    CapdTraceResult run_capd_trace(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        bool forward,
        const NearestRounding& nr,
        int n_steps = 50);

} // namespace dreal
