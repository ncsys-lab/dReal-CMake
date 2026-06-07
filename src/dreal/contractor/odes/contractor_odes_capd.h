#pragma once
// C++17 interface for the CAPD v6 ODE integration backend.
// The implementation (contractor_odes_capd.cc) is also compiled at C++17.
//
// This contractor runs alongside the Codac CtcLohner contractor as a second
// ODE backend. CAPD's Taylor-order-20 integrator handles long-horizon and
// high-dimensional flows where Codac's order-2 ceiling causes the per-step
// enclosure to widen out of usefulness. The dispatch is gated in
// contractor_odes.cc by (t_ub, n_state_vars); see CLAUDE.md and the plan
// notes for the gating policy.

#include <memory>
#include <utility>
#include <vector>

#include "dreal/symbolic/symbolic.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"

namespace dreal
{
    // Result type mirroring CodacOdeResult so contractor_odes.cc can dispatch
    // to either backend with no shape change in the caller.
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

    // Build a cache for `flow`. Returns nullptr if expression translation
    // into the capd::IMap string format fails (e.g. an unsupported
    // ExpressionKind in the RHS).
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

    // Run CAPD forward integration via Taylor-order-20.
    //
    // Forward step: integrate the cached f(x) IMap from u0_bounds = X_0 over
    // [0, t_ub] using capd::IOdeSolver(order=20) + capd::ITimeMap. Intersect
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
    CapdOdeResult run_capd_fwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& X_t_bounds,
        double t_ub,
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
        double t_ub,
        int n_steps_hint = 20);

} // namespace dreal
