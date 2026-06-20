// CAPD v6 Taylor ODE contractor TU. C++17. (Taylor order is the tunable
// kCapdTaylorOrder constant below — default 10; see OPTIMIZATION_LOG.md.)
//
// CAPD is the sole ODE backend. It is always used for a non-trivial flow (the
// trivial-flow short-circuit in contractor_odes.cc handles the all-zero-RHS
// case separately); there is no longer any backend dispatch or gating. The
// previous Codac CtcLohner backend and the Codac/CAPD gated hybrid (with its
// --capd-t-gate / --capd-ndim-gate flags) were retired — see CODAC_MIGRATION.md
// for that history. CAPD's high-order Taylor IOdeSolver gives tight enclosures
// on long-horizon / high-dimensional dynamics (e.g. the 15-var quad flow with
// sin/cos sub-trees) where a low fixed order would widen out of usefulness.

#include "contractor_odes_capd.h"
#include "to_capd_string.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
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
    // CAPD solver numeric configuration — single source of truth.
    //
    // These are the performance-critical knobs for the rigorous Taylor
    // integrator, shared by run_capd_fwd / run_capd_bwd / run_capd_trace.
    // Profiling (a `sample` of the tacas inverter family) shows
    // computeTaylorCoefficients at this order is ~74% of solve time on
    // nonlinear ODE benchmarks, so the order is the primary speed lever.
    // Lowering the order or loosening the tolerances widens the enclosure
    // (still sound — an over-approximation can never cause a false-UNSAT) in
    // exchange for cheaper steps. See OPTIMIZATION_LOG.md for the sweep history.
    //
    // constexpr at namespace scope has internal linkage, so these are private
    // to this TU.
    constexpr int    kCapdTaylorOrder  = 10;
    constexpr double kCapdAbsTolerance = 1e-10;
    constexpr double kCapdRelTolerance = 1e-10;

    // C0 set representation for the rigorous enclosure, shared by the fwd/bwd/
    // trace integrators. capd::C0Rect2Set is a doubleton (x + C*r0 + Q*q) with
    // QR reorganization — CAPD's general-purpose default. Tighter alternatives
    // (C0TripletonSet, C0HORect2Set = Hermite-Obreshkov corrector) trade higher
    // per-step cost for less wrapping; whether that nets out is benchmark-
    // dependent, hence the single alias here for A/B testing.
    using CapdC0Set = capd::C0Rect2Set;

    // Apply the shared tolerances to a freshly-constructed CAPD solver. The
    // order is a constructor argument (kCapdTaylorOrder) at each call site.
    template <typename Solver>
    inline void configure_capd_solver(Solver& solver) {
        solver.setAbsoluteTolerance(kCapdAbsTolerance);
        solver.setRelativeTolerance(kCapdRelTolerance);
    }

    // -------------------------------------------------------------------------
    // CapdOdeCache — opaque wrapper holding the per-flow IMap objects.
    //
    // capd::IMap parses the RHS string at construction and builds the
    // automatic-differentiation tree CAPD's solver needs. This is the
    // expensive step; we translate once and amortize it via the cache.
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
        int        n_state_vars;  // # of TRUE state vars (= var: count = set dim)
        bool       trivial; // every RHS is the literal 0
        // CAPD parameter names (flow variables with d/dt == 0), in ode_list
        // order. Bound per-call from the box via setParameter. The integrated
        // system has dimension n_state_vars; parameters are NOT integrated.
        std::vector<std::string> par_names;

        CapdOdeCache(capd::IMap f, capd::IMap f_neg, int n, bool is_trivial,
                     std::vector<std::string> pars)
            : fn_fwd(std::move(f)),
              fn_bwd(std::move(f_neg)),
              n_state_vars(n),
              trivial(is_trivial),
              par_names(std::move(pars)) {}
    };

    bool capd_ode_cache_is_trivial(const std::shared_ptr<CapdOdeCache>& c) {
        return c && c->trivial;
    }

    // -------------------------------------------------------------------------
    // Build the capd::IMap string from the OdeFlow.
    //
    // Format: "var:x,v;par:a;fun:(a*v),(-9.8);"
    //
    // Parameters: flow variables whose d/dt is the literal 0 are constant along
    // the trajectory (OdeFlow classifies these as "pars"). They are emitted in
    // a "par:" section and bound per-call via setParameter, NOT integrated.
    // This must match the C0Rect2Set, which is built from only the true state
    // variables: if parameters were emitted as integration variables the IMap
    // dimension (and its n×n Jacobian) would exceed the set's buffers and
    // overrun the heap. (The previous var-only form had exactly that bug,
    // crashing on automaton models that carry mode/guard parameters.)
    // -------------------------------------------------------------------------

    namespace {
        struct ImapStrings {
            std::string fwd;
            std::string bwd;
            // CAPD parameter names (d/dt == 0 flow vars), in ode_list order.
            std::vector<std::string> par_names;
            // Number of TRUE state variables = var: count = integration dim.
            int n_vars{0};
        };

        // Build "var:...;par:...;fun:f1,...,fn;" (forward) and the same with
        // each RHS negated (backward). Throws on translation failure of any
        // RHS.
        //
        // Flow variables split into two roles (see OdeFlow ctor: a var is a
        // "par" iff its d/dt is the literal 0, i.e. it is constant along the
        // flow):
        //   - true state variables -> "var:" + an entry in "fun:"; integrated.
        //   - parameters           -> "par:"; constant, bound per-call via
        //                             setParameter, NOT integrated.
        // Emitting parameters as integration variables (the previous behavior)
        // made the IMap dimension exceed the C0Rect2Set built from only the
        // true-var bounds, so CAPD's n×n Jacobian write overran the set's
        // buffers -> heap corruption / crash on automaton models.
        ImapStrings build_imap_strings(
            const OdeFlow& flow,
            const std::vector<Variable>& ordered_vars)
        {
            std::unordered_map<Variable::Id, Expression> rhs_map;
            for (const auto& [var, rhs] : flow.ode_list)
                rhs_map[var.get_id()] = rhs;

            // All-parameter (trivial) flow: CAPD rejects an empty var:/fun:.
            // Such flows are intercepted by the trivial-flow short-circuit in
            // contractor_odes.cc and never reach run_capd_*, but we still need
            // a valid (non-null) IMap so the cache is built. Fall back to the
            // every-variable-is-an-integration-variable form (no par:).
            bool any_var = false;
            for (const auto& var : ordered_vars)
                if (!flow.is_par(var)) { any_var = true; break; }
            const bool all_par = !any_var;

            std::vector<std::string> par_names;
            std::ostringstream var_part, par_part, fwd_fn, bwd_fn;
            var_part << "var:";
            fwd_fn << "fun:";
            bwd_fn << "fun:";
            bool first_var = true;
            int n_vars = 0;
            for (const auto& var : ordered_vars) {
                if (!all_par && flow.is_par(var)) {
                    par_names.push_back(var.get_name());
                    continue;
                }
                if (!first_var) {
                    var_part << ',';
                    fwd_fn << ',';
                    bwd_fn << ',';
                }
                first_var = false;
                ++n_vars;
                var_part << var.get_name();
                const std::string rhs_str = to_capd_string(rhs_map.at(var.get_id()));
                fwd_fn << rhs_str;
                bwd_fn << "(0-(" << rhs_str << "))";
            }
            var_part << ';';
            fwd_fn << ';';
            bwd_fn << ';';

            std::string par_section;
            if (!par_names.empty()) {
                par_part << "par:";
                for (size_t i = 0; i < par_names.size(); ++i) {
                    if (i) par_part << ',';
                    par_part << par_names[i];
                }
                par_part << ';';
                par_section = par_part.str();
            }

            ImapStrings out;
            out.fwd = var_part.str() + par_section + fwd_fn.str();
            out.bwd = var_part.str() + par_section + bwd_fn.str();
            out.par_names = std::move(par_names);
            out.n_vars = n_vars;
            return out;
        }

        // Return a parameter-bound view of the cached (parsed) IMap with its
        // CAPD parameters bound to the caller's current intervals.
        //
        // We cannot mutate the cached IMap in place (it is shared across
        // parallel ICP workers and setParameter mutates), and a fresh per-call
        // deep copy of `base` rebuilds the whole automatic-differentiation tree
        // — ~part of the ~8% allocation churn at order 10, since the integration
        // itself got cheap. Instead we keep one reusable copy per (thread, base
        // map) in a thread_local cache: the AD tree is copied once per thread,
        // and each call only re-binds the parameters (cheap setParameter) into
        // that copy. This is behavior-identical to copying `base` fresh each
        // call — setParameter fully overwrites the named parameters, the cached
        // base maps are immutable and live for the whole process, and the
        // thread_local storage means no copy is ever shared across workers.
        //
        // The returned reference is valid until the next with_params call for
        // the same base map on this thread; each run_capd_* call binds, hands
        // the map to a local solver, integrates to completion, and returns
        // before the next bind, so there is no aliasing within a thread.
        // par_bounds is index-aligned with par_names (both in ode_list
        // parameter order).
        capd::IMap& with_params(
            const capd::IMap& base,
            const std::vector<std::string>& par_names,
            const std::vector<std::pair<double, double>>& par_bounds)
        {
            thread_local std::unordered_map<const capd::IMap*, capd::IMap> tls;
            auto it = tls.find(&base);
            if (it == tls.end())
                it = tls.emplace(&base, base).first;  // one AD-tree copy per thread
            capd::IMap& m = it->second;
            // Sizes are equal by construction (par_names and par_bounds both
            // come from this flow's parameter list, in ode_list order). A
            // mismatch is a programming error, not a runtime condition to
            // tolerate, so we index par_bounds directly.
            for (size_t i = 0; i < par_names.size(); ++i)
                m.setParameter(par_names[i],
                               capd::interval(par_bounds[i].first,
                                              par_bounds[i].second));
            return m;
        }

        // Per-process cache slot.
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

        // Step-count hint for the integration horizon. capd::IOdeSolver
        // chooses its actual step size internally from order + target
        // tolerance; this just bounds how the horizon is subdivided.
        // Currently only run_capd_trace consumes the result (to slice the
        // --visualize trajectory); run_capd_fwd/bwd compute it but leave the
        // real stepping to CAPD's adaptive control (the derived max_step is
        // not imposed on the solver). See OPTIMIZATION_LOG.md (tolerance entry).
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
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // CAPD's IMap parser/builder leaves the FPU in a directed mode
        // (FE_UPWARD) on return — it does NOT restore nearest. Contain that
        // known clobber here so callers (and their NearestRoundingScope checks)
        // see nearest on return. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
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

        // Raise — do not silently skip — if the flow cannot be translated.
        // A null cache would make the contractor's Prune a no-op, leaving the
        // ODE constraint un-narrowed: sound (no narrowing never over-prunes)
        // but a silent under-enforcement that hides an unhandled ODE from the
        // user. Well-formed flows over the supported expression set never hit
        // these paths; if one does, the right answer is a loud failure naming
        // the cause, not a quietly weaker solve. (There is no Codac fallback.)
        ImapStrings strs;
        try {
            strs = build_imap_strings(*flow, ordered_vars);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("CAPD ODE contractor: cannot translate an ODE flow "
                            "RHS to a CAPD vector field (") + e.what() +
                "). The RHS uses an expression kind to_capd_string does not "
                "support (e.g. if-then-else or an uninterpreted function). "
                "Refusing to silently skip the ODE constraint.");
        }

        try {
            capd::IMap fn_fwd(strs.fwd);
            capd::IMap fn_bwd(strs.bwd);
            auto cache = std::make_shared<CapdOdeCache>(
                std::move(fn_fwd), std::move(fn_bwd),
                strs.n_vars, is_trivial, std::move(strs.par_names));
            std::lock_guard<std::mutex> lock(flow_cache_mutex());
            auto& m = flow_cache_map();
            auto [it, _inserted] = m.try_emplace(
                flow_ptr, CacheSlot{std::move(flow), std::move(cache)});
            return it->second.cache;
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("CAPD ODE contractor: capd::IMap rejected the "
                            "generated vector-field string (") + e.what() +
                "). Forward field was: '" + strs.fwd +
                "'. Refusing to silently skip the ODE constraint.");
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
    // enclosure with X_t. X_0 narrowing is left to the standalone BWD
    // contractor (theory_solver.cc instantiates one per ODE constraint and
    // queues it alongside this one in the fixpoint loop), mirroring cav26.
    // -------------------------------------------------------------------------

    CapdOdeResult run_capd_fwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& X_t_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        const NearestRounding& /*nr*/,
        int n_steps_hint)
    {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // Contain CAPD's directed-mode clobber so this adapter is nearest-in /
        // nearest-out. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
        CapdOdeResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps_hint <= 0) return result;

        const int n_steps = adaptive_n_steps(t_ub, n_steps_hint);
        const double max_step = t_ub / n_steps;
        if (max_step <= 0.0) return result;

        capd::IVector terminal_fwd(n);
        try {
            // Parameter-bound view of the cached map (thread_local; see
            // with_params). Must outlive solver_fwd, which holds a reference to
            // it — the thread_local backing storage does.
            capd::IMap& map_fwd = with_params(cache->fn_fwd, cache->par_names, par_bounds);
            capd::IOdeSolver solver_fwd(map_fwd, kCapdTaylorOrder);
            configure_capd_solver(solver_fwd);
            capd::ITimeMap time_map_fwd(solver_fwd);

            CapdC0Set set(to_ivector(u0_bounds));
            terminal_fwd = time_map_fwd(t_ub, set);
        } catch (const std::exception&) {
            return result;
        }

        if (!intersect_into(terminal_fwd, X_t_bounds, result.vars_t_narrowed)) {
            return result;
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
    // This is a single backward integration (no per-step intersection loop):
    // CAPD integrates the cached negated field -f(x) forward over [0, t_ub],
    // which is reverse-time integration of f(x), giving the backward image
    // directly.
    // -------------------------------------------------------------------------

    CapdOdeResult run_capd_bwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& Xt_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        const NearestRounding& /*nr*/,
        int n_steps_hint)
    {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // Contain CAPD's directed-mode clobber so this adapter is nearest-in /
        // nearest-out. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
        CapdOdeResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps_hint <= 0) return result;

        const int n_steps = adaptive_n_steps(t_ub, n_steps_hint);
        const double max_step = t_ub / n_steps;
        if (max_step <= 0.0) return result;

        try {
            capd::IMap& map_bwd = with_params(cache->fn_bwd, cache->par_names, par_bounds);
            capd::IOdeSolver solver_bwd(map_bwd, kCapdTaylorOrder);
            configure_capd_solver(solver_bwd);
            capd::ITimeMap time_map_bwd(solver_bwd);

            CapdC0Set set(to_ivector(Xt_bounds));
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

    // -------------------------------------------------------------------------
    // run_capd_trace
    //
    // Integrate the chosen RHS (fn_fwd / fn_bwd) from u0 over [0, t_ub] and
    // record one CapdTracePoint per equally-spaced sub-slice. Each point
    // captures the slice (t_prev, t_now) and the enclosure at t_now (which
    // covers all states reachable up to that time since the previous slice).
    //
    // ITimeMap advances `set` in place across successive calls. We rely on
    // that: the i-th call returns the enclosure at t_i = i * (t_ub / n_steps),
    // having advanced `set` from the result of call i-1.
    // -------------------------------------------------------------------------

    CapdTraceResult run_capd_trace(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        bool forward,
        const NearestRounding& /*nr*/,
        int n_steps)
    {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // Contain CAPD's directed-mode clobber so this adapter is nearest-in /
        // nearest-out. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
        CapdTraceResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps <= 0) return result;
        if (u0.size() != static_cast<size_t>(n)) return result;

        try {
            capd::IMap& chosen_map = with_params(
                forward ? cache->fn_fwd : cache->fn_bwd,
                cache->par_names, par_bounds);
            capd::IOdeSolver solver(chosen_map, kCapdTaylorOrder);
            configure_capd_solver(solver);
            capd::ITimeMap time_map(solver);

            CapdC0Set set(to_ivector(u0));

            const double dt = t_ub / static_cast<double>(n_steps);
            double t_prev = 0.0;
            result.points.reserve(static_cast<size_t>(n_steps));

            for (int i = 1; i <= n_steps; ++i) {
                const double t_i = (i == n_steps) ? t_ub
                                                  : static_cast<double>(i) * dt;
                const capd::IVector encl = time_map(t_i, set);

                CapdTracePoint pt;
                pt.t_lb = t_prev;
                pt.t_ub = t_i;
                pt.var_enclosures.reserve(static_cast<size_t>(n));
                for (int j = 0; j < n; ++j) {
                    pt.var_enclosures.emplace_back(encl[j].leftBound(),
                                                   encl[j].rightBound());
                }
                result.points.push_back(std::move(pt));
                t_prev = t_i;
            }
            result.succeeded = true;
        } catch (const std::exception&) {
            // Partial trace remains in result.points; succeeded stays false.
        }

        return result;
    }

} // namespace dreal
