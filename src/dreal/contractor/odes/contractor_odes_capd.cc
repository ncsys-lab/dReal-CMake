// CAPD v6 Taylor-order-20 ODE contractor TU. C++17.
//
// This is the second ODE backend living alongside contractor_odes_codac.cc.
// The dispatch is gated in contractor_odes.cc by (t_ub, n_state_vars); see
// CLAUDE.md for the gate flags and CODAC_MIGRATION.md for the motivation.
//
// Why CAPD as a second backend: Codac's CtcLohner is order-2 Taylor (a
// hardcoded ceiling). Long-horizon or high-dimensional dynamics (e.g. the
// 15-var quad flow with sin/cos sub-trees) widen out of usefulness at
// order 2 — even with adaptive n_steps. CAPD's order-20 IOdeSolver
// recovers the tightness; the gate keeps it from paying CAPD's per-call
// cost on the Lohner-friendly easy benchmarks.

#include "contractor_odes_capd.h"
#include "to_capd_string.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// CAPD public umbrella header — pulls in IMap, IOdeSolver, ITimeMap,
// C0Rect2Set, IVector, interval, and the basic algebra types.
#include "capd/capdlib.h"

namespace dreal
{
    // -------------------------------------------------------------------------
    // CapdOdeCache — opaque wrapper holding the per-flow IMap objects.
    //
    // capd::IMap parses the RHS string at construction and builds the
    // automatic-differentiation tree CAPD's solver needs. This is the
    // expensive step; we amortize it via the cache, mirroring the Codac
    // TU's translate-once policy.
    //
    // capd::IOdeSolver and capd::ITimeMap carry mutable step-size and
    // working state, so they are *not* safe to share across parallel ICP
    // workers. We construct them per-call inside run_capd_*. Cost is
    // negligible compared to the integration itself.
    // -------------------------------------------------------------------------

    class CapdOdeCache {
    public:
        capd::IMap fn_fwd;   // f(x)
        capd::IMap fn_bwd;   // -f(x), used for backward integration
        int        n_state_vars;
        bool       trivial; // every RHS is the literal 0

        CapdOdeCache(capd::IMap f, capd::IMap f_neg, int n, bool is_trivial)
            : fn_fwd(std::move(f)),
              fn_bwd(std::move(f_neg)),
              n_state_vars(n),
              trivial(is_trivial) {}
    };

    bool capd_ode_cache_is_trivial(const std::shared_ptr<CapdOdeCache>& c) {
        return c && c->trivial;
    }

    // -------------------------------------------------------------------------
    // Build the capd::IMap string from the OdeFlow.
    //
    // Format: "var:x,v;fun:v,(-9.8);"
    //
    // Parameters: the original CAPD contractor used "par:a,b;" for flow
    // parameters; we don't have free symbolic parameters in this ODE flow
    // (all parameters are intersected at flow construction time, see
    // intersect_params in contractor_odes.cc), so the var-only form is
    // sufficient.
    // -------------------------------------------------------------------------

    namespace {
        struct ImapStrings {
            std::string fwd;
            std::string bwd;
        };

        // Build "var:...;fun:f1,...,fn;" (forward) and the same with each
        // RHS negated (backward). Throws on translation failure of any RHS.
        ImapStrings build_imap_strings(
            const OdeFlow& flow,
            const std::vector<Variable>& ordered_vars)
        {
            std::ostringstream var_part;
            var_part << "var:";
            for (size_t i = 0; i < ordered_vars.size(); ++i) {
                if (i) var_part << ',';
                var_part << ordered_vars[i].get_name();
            }
            var_part << ';';

            std::unordered_map<Variable::Id, Expression> rhs_map;
            for (const auto& [var, rhs] : flow.ode_list)
                rhs_map[var.get_id()] = rhs;

            std::ostringstream fwd_fn;
            std::ostringstream bwd_fn;
            fwd_fn << "fun:";
            bwd_fn << "fun:";
            for (size_t i = 0; i < ordered_vars.size(); ++i) {
                if (i) {
                    fwd_fn << ',';
                    bwd_fn << ',';
                }
                const std::string rhs_str =
                    to_capd_string(rhs_map.at(ordered_vars[i].get_id()));
                fwd_fn << rhs_str;
                // Wrap negation in (0-(...)) — `-(...)` is also valid in
                // capd::IMap but the (0-...) form sidesteps any
                // unary-minus parsing ambiguity in older CAPD parsers.
                bwd_fn << "(0-(" << rhs_str << "))";
            }
            fwd_fn << ';';
            bwd_fn << ';';

            ImapStrings out;
            out.fwd = var_part.str() + fwd_fn.str();
            out.bwd = var_part.str() + bwd_fn.str();
            return out;
        }

        // Per-process cache slot — identical pattern to the Codac TU.
        // shared_ptr<const OdeFlow> in the slot keeps the keyed OdeFlow
        // alive so its raw-pointer key cannot be reused for a different
        // flow (closes the same pointer-reuse hazard).
        struct CacheSlot {
            std::shared_ptr<const OdeFlow> flow;
            std::shared_ptr<CapdOdeCache> cache;
        };
        std::mutex& flow_cache_mutex() {
            static std::mutex m;
            return m;
        }
        std::unordered_map<const OdeFlow*, CacheSlot>&
        flow_cache_map() {
            static std::unordered_map<const OdeFlow*, CacheSlot> m;
            return m;
        }

        // Adaptive max-step policy matching the Codac TU. capd::IOdeSolver
        // chooses its actual step size internally based on order + target
        // tolerance, but the time horizon must be reachable without
        // endlessly subdividing. We expose n_steps_hint and let CAPD's
        // adaptive logic do the rest.
        int adaptive_n_steps(double t_ub, int n_steps_hint) {
            return std::clamp<int>(
                std::max(n_steps_hint,
                         static_cast<int>(std::ceil(t_ub * 2.0))),
                n_steps_hint, 60);
        }
    } // namespace

    std::shared_ptr<CapdOdeCache> make_capd_ode_cache(
        std::shared_ptr<const OdeFlow> flow,
        const std::vector<Variable>& ordered_vars)
    {
        if (!flow) return nullptr;
        const OdeFlow* const flow_ptr = flow.get();

        // Fast path — existing cache for this flow pointer.
        {
            std::lock_guard<std::mutex> lock(flow_cache_mutex());
            auto& m = flow_cache_map();
            auto it = m.find(flow_ptr);
            if (it != m.end()) return it->second.cache;
        }

        // Trivial-flow detection: every RHS is the literal 0. CAPD copes
        // fine with constant-zero RHS but the trivial-flow short-circuit in
        // contractor_odes.cc bypasses CAPD entirely for these — we still
        // build the IMap so any unanticipated code path stays safe.
        bool is_trivial = true;
        for (const auto& [_var, rhs] : flow->ode_list) {
            if (!is_zero(rhs)) { is_trivial = false; break; }
        }

        ImapStrings strs;
        try {
            strs = build_imap_strings(*flow, ordered_vars);
        } catch (const std::exception&) {
            // Expression translation failed (unsupported kind in RHS).
            // Caller falls back to the Codac contractor.
            return nullptr;
        }

        try {
            capd::IMap fn_fwd(strs.fwd);
            capd::IMap fn_bwd(strs.bwd);
            auto cache = std::make_shared<CapdOdeCache>(
                std::move(fn_fwd), std::move(fn_bwd),
                static_cast<int>(ordered_vars.size()), is_trivial);
            std::lock_guard<std::mutex> lock(flow_cache_mutex());
            auto& m = flow_cache_map();
            auto [it, _inserted] = m.try_emplace(
                flow_ptr, CacheSlot{std::move(flow), std::move(cache)});
            return it->second.cache;
        } catch (const std::exception&) {
            // capd::IMap parser rejected the string (typically an
            // unsupported syntactic form — e.g. a function we don't emit).
            return nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    namespace {
        // Build a capd::IVector from a [(lb, ub), ...] list.
        capd::IVector to_ivector(const std::vector<std::pair<double, double>>& v) {
            capd::IVector out(static_cast<int>(v.size()));
            for (size_t i = 0; i < v.size(); ++i)
                out[static_cast<int>(i)] = capd::interval(v[i].first, v[i].second);
            return out;
        }

        // Intersect a CAPD enclosure with the caller's bounds, returning
        // (false, _) if any component is infeasible.
        bool intersect_into(
            const capd::IVector& encl,
            const std::vector<std::pair<double, double>>& bounds,
            std::vector<std::pair<double, double>>& out)
        {
            const int n = encl.dimension();
            out.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                const double encl_lo = encl[i].leftBound();
                const double encl_hi = encl[i].rightBound();
                const double new_lo = std::max(bounds[static_cast<size_t>(i)].first,  encl_lo);
                const double new_hi = std::min(bounds[static_cast<size_t>(i)].second, encl_hi);
                if (new_lo > new_hi) return false;
                out.emplace_back(new_lo, new_hi);
            }
            return true;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // FWD: forward-integrate f(x) from X_0 over [0, t_ub], intersect terminal
    // enclosure with X_t. Then backward-integrate -f(x) from the narrowed X_t
    // back to t=0, intersect with X_0. Together these mirror Codac's
    // CtcLohner FWD_BWD joint narrowing.
    // -------------------------------------------------------------------------

    CapdOdeResult run_capd_fwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& X_t_bounds,
        double t_ub,
        int n_steps_hint)
    {
        CapdOdeResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps_hint <= 0) return result;

        const int n_steps = adaptive_n_steps(t_ub, n_steps_hint);
        const double max_step = t_ub / n_steps;
        if (max_step <= 0.0) return result;

        // ----- Step 1: forward integrate f(x) from X_0 -----
        capd::IVector terminal_fwd(n);
        try {
            capd::IOdeSolver solver_fwd(cache->fn_fwd, /*order=*/20);
            solver_fwd.setAbsoluteTolerance(1e-10);
            solver_fwd.setRelativeTolerance(1e-10);
            capd::ITimeMap time_map_fwd(solver_fwd);
            // No hard step cap — CAPD picks step adaptively. n_steps_hint
            // controls only our intersection-quality expectations downstream.

            capd::C0Rect2Set set(to_ivector(u0_bounds));
            terminal_fwd = time_map_fwd(t_ub, set);
        } catch (const std::exception&) {
            // Divergence / step-control failure — bail. Caller falls back
            // to Lohner (or accepts no narrowing this Prune call).
            return result;
        }

        if (!intersect_into(terminal_fwd, X_t_bounds, result.vars_t_narrowed)) {
            // Infeasible: no trajectory from X_0 reaches X_t — caller can
            // mark the box empty on the strength of this. We set found=true
            // with an empty vars_t_narrowed to distinguish "definitively
            // empty" from "no narrowing".
            // Caller convention from the Codac TU: an empty vars_t_narrowed
            // is treated as "no result". To stay symmetric, we report
            // not-found here and let the per-pass narrowing loop decide.
            // (The infeasibility is then surfaced by the IBEX layer once
            // the box becomes empty through other contractors.)
            return result;
        }

        // ----- Step 2: backward integrate -f(x) from narrowed X_t -----
        // This is the "free" BWD pass that joint-narrows X_0. If anything
        // goes wrong, we keep the forward narrowing and return.
        try {
            capd::IOdeSolver solver_bwd(cache->fn_bwd, /*order=*/20);
            solver_bwd.setAbsoluteTolerance(1e-10);
            solver_bwd.setRelativeTolerance(1e-10);
            capd::ITimeMap time_map_bwd(solver_bwd);

            capd::C0Rect2Set set_bwd(to_ivector(result.vars_t_narrowed));
            const capd::IVector start_encl = time_map_bwd(t_ub, set_bwd);

            std::vector<std::pair<double, double>> vars_0_narrowed;
            if (intersect_into(start_encl, u0_bounds, vars_0_narrowed)) {
                result.vars_0_narrowed = std::move(vars_0_narrowed);
            }
            // If the bwd intersect is empty, leave vars_0_narrowed empty
            // and let the forward narrowing alone drive the change.
        } catch (const std::exception&) {
            // BWD diverged — keep FWD result.
        }

        result.found = true;
        result.t_new_lb = 0.0;
        result.t_new_ub = t_ub;
        return result;
    }

    // -------------------------------------------------------------------------
    // BWD one-shot: integrate -f(x) from X_t over [0, t_ub]. The terminal
    // enclosure is the backward image of X_t — i.e. the set of states at
    // real time 0 whose forward trajectory under f(x) reaches X_t. Returned
    // as vars_t_narrowed so the caller (in its swapped frame where
    // m_vars_t = original X_0) intersects it with the right gate.
    //
    // Mirrors Codac's run_lohner_bwd_oneshot but uses true CAPD backward
    // integration via the negated IMap instead of Codac's
    // LohnerAlgorithm(forward=false) reverse-time mode.
    // -------------------------------------------------------------------------

    CapdOdeResult run_capd_bwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& Xt_bounds,
        double t_ub,
        int n_steps_hint)
    {
        CapdOdeResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps_hint <= 0) return result;

        const int n_steps = adaptive_n_steps(t_ub, n_steps_hint);
        const double max_step = t_ub / n_steps;
        if (max_step <= 0.0) return result;

        try {
            capd::IOdeSolver solver_bwd(cache->fn_bwd, /*order=*/20);
            solver_bwd.setAbsoluteTolerance(1e-10);
            solver_bwd.setRelativeTolerance(1e-10);
            capd::ITimeMap time_map_bwd(solver_bwd);

            capd::C0Rect2Set set(to_ivector(Xt_bounds));
            const capd::IVector encl = time_map_bwd(t_ub, set);

            result.vars_t_narrowed.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
                result.vars_t_narrowed.emplace_back(encl[i].leftBound(),
                                                    encl[i].rightBound());

            result.found = true;
            result.t_new_lb = 0.0;
            result.t_new_ub = t_ub;
        } catch (const std::exception&) {
            // Backward integration diverged — return no narrowing.
        }

        return result;
    }

} // namespace dreal
