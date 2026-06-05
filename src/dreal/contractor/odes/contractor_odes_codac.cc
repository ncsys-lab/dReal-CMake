// This file MUST be compiled with C++20 (Codac v2 requires concepts, std::numbers).
// CMakeLists.txt sets COMPILE_OPTIONS -std=c++20 for this file only.

#include "contractor_odes_codac.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <codac2_CtcLohner.h>
#include <codac2_TDomain.h>
#include <codac2_SlicedTube.h>
#include <codac2_TimePropag.h>
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
    // CodacOdeCache — opaque wrapper around the per-flow Codac state
    //
    // Holding the AnalyticFunction and CtcLohner across calls saves the
    // expression-tree translation (which dominates per-call cost on ODE-heavy
    // benchmarks) plus the CtcLohner setup. CtcLohner::contract is const, so
    // sharing the same instance across parallel ICP workers is safe.
    // -------------------------------------------------------------------------

    class CodacOdeCache {
    public:
        codac2::AnalyticFunction<codac2::VectorType> fn;
        codac2::CtcLohner                            ctc;
        int                                          n_state_vars;
        // True iff every RHS in the flow is the literal constant 0 — the
        // trajectory of every state variable is constant and X_0 ∩ X_t is
        // the only consistent assignment. We detect this so the caller can
        // bypass CtcLohner entirely on degenerate flows (e.g. the
        // d/dt[d]=0 planning benchmark with 1280 modes).
        bool                                         trivial;

        CodacOdeCache(codac2::AnalyticFunction<codac2::VectorType> f,
                      int n,
                      int contractions,
                      double eps,
                      bool is_trivial)
            : fn(std::move(f)),
              ctc(fn, contractions, eps),
              n_state_vars(n),
              trivial(is_trivial) {}
    };

    bool codac_ode_cache_is_trivial(const std::shared_ptr<CodacOdeCache>& c) {
        return c && c->trivial;
    }
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
    // Cache factory — translate the ODE flow once, build the CtcLohner once.
    //
    // Hybrid systems often replicate one flow across dozens of modes (each
    // mode is a separate Integral formula but shares a single flow_ptr).
    // The theory solver builds one contractor per (formula, direction) pair,
    // so without per-flow deduplication we'd translate the same RHS expressions
    // into Codac's expression tree N_modes × 2 times. We key by flow address;
    // OdeFlow objects are held via shared_ptr from the moment they're parsed,
    // so addresses are stable for the lifetime of any contractor.
    // -------------------------------------------------------------------------

    namespace {
        std::mutex& flow_cache_mutex() {
            static std::mutex m;
            return m;
        }
        std::unordered_map<const OdeFlow*, std::shared_ptr<CodacOdeCache>>&
        flow_cache_map() {
            static std::unordered_map<const OdeFlow*, std::shared_ptr<CodacOdeCache>> m;
            return m;
        }
    }

    std::shared_ptr<CodacOdeCache> make_codac_ode_cache(
        const OdeFlow& flow,
        const std::vector<Variable>& ordered_vars)
    {
        // Fast path: existing cache for this flow pointer.
        {
            std::lock_guard<std::mutex> lock(flow_cache_mutex());
            auto& m = flow_cache_map();
            auto it = m.find(&flow);
            if (it != m.end()) return it->second;
        }

        auto fn_opt = build_ode_fn(flow, ordered_vars);
        if (!fn_opt) return nullptr;
        const int n = static_cast<int>(ordered_vars.size());

        // Trivial-flow detection: every RHS is the literal constant 0.
        // We still build the AnalyticFunction (cheap) and CtcLohner (cheap)
        // so generate_trace and any unanticipated code path remains safe,
        // but mark the cache so Prune() can short-circuit.
        bool is_trivial = true;
        for (const auto& [_var, rhs] : flow.ode_list) {
            if (!is_zero(rhs)) { is_trivial = false; break; }
        }

        // contractions=2 is the speed/tightness sweet spot.
        //   * contractions=5 (pre-cache default): tight enclosures, but
        //     CtcLohner cost dominated Prune time on ODE-heavy benchmarks
        //     (bouncing_ball_with_drag_10_0: 13s → 3s by going to 2).
        //   * contractions=1 (LohnerAlgorithm default): fastest per call,
        //     but enclosures so wide that benchmarks like cardiac needed
        //     many more ICP bisections — *net* slower.
        // eps=0.1 is CtcLohner's default global-enclosure inflation.
        // Tighter values (0.05) gave no measurable win on bouncing ball
        // or cardiac and risk GlobalEnclosureError on untested dynamics.
        auto cache = std::make_shared<CodacOdeCache>(
            std::move(*fn_opt), n,
            /*contractions=*/2, /*eps=*/0.1, is_trivial);

        {
            std::lock_guard<std::mutex> lock(flow_cache_mutex());
            auto& m = flow_cache_map();
            auto [it, inserted] = m.try_emplace(&flow, std::move(cache));
            return it->second;
        }
    }

    // -------------------------------------------------------------------------
    // Public entry point — uses CtcLohner with FWD_BWD for joint endpoint contraction
    // -------------------------------------------------------------------------

    CodacOdeResult run_lohner_integration(
        const std::shared_ptr<CodacOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& X_t_bounds,
        double t_ub,
        bool /*forward*/,  // CtcLohner FWD_BWD handles both directions jointly
        int n_steps_hint)
    {
        CodacOdeResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps_hint <= 0) return result;

        // Adaptive step count for long time horizons.
        //
        // CtcLohner uses Taylor order 2 (per-step error O(h^3)). With the
        // historical n_steps=20 cap, h scales linearly with t_ub: cardiac
        // (t_ub up to 30) was running at h=1.5 - far too coarse, so most
        // of the contractions budget burned widening the per-step
        // enclosure back to soundness instead of narrowing toward Xt.
        //
        // Strategy: keep n_steps=20 as the *floor* (never reduce - short
        // horizons like bouncing ball's t_ub in [0,3] already use small h,
        // and we lose nothing by leaving them alone) and only ADD steps
        // when t_ub * 2 > n_steps_hint, i.e. t_ub > 10. This targets
        // h <= 0.5 for long horizons while paying zero overhead on the
        // common case. Capped at 60 to bound per-call cost.
        const int n_steps = std::clamp<int>(
            std::max(n_steps_hint,
                     static_cast<int>(std::ceil(t_ub * 2.0))),
            n_steps_hint, 60);
        const double h = t_ub / n_steps;
        if (h <= 0.0) return result;

        // Build endpoint interval vectors
        codac2::IntervalVector X0(n), Xt(n);
        for (int i = 0; i < n; ++i) {
            X0[i] = codac2::Interval(u0_bounds[static_cast<std::size_t>(i)].first,
                                      u0_bounds[static_cast<std::size_t>(i)].second);
            Xt[i] = codac2::Interval(X_t_bounds[static_cast<std::size_t>(i)].first,
                                      X_t_bounds[static_cast<std::size_t>(i)].second);
        }

        // Tube initialization: hull of endpoint intervals, inflated to give
        // the trajectory room to evolve between the gates.
        //
        // The envelope must enclose every trajectory that satisfies both
        // gates — too tight and CtcLohner throws GlobalEnclosureError
        // (we return no narrowing); too wide and the algorithm wastes
        // contractions narrowing it. The previous 10× max-radius factor
        // was very conservative; 3× still avoids GlobalEnclosureError on
        // the benchmarks tested while cutting per-call work materially.
        codac2::IntervalVector init_box = X0 | Xt;
        double max_rad = 0.0;
        for (int i = 0; i < n; ++i)
            max_rad = std::max(max_rad, init_box[i].rad());
        const double inflate_by = std::max(1.0, max_rad) * 3.0;
        for (int i = 0; i < n; ++i)
            init_box[i] = init_box[i].inflate(inflate_by);

        try {
            auto tdomain = codac2::create_tdomain(
                codac2::Interval(0., t_ub), h, /*with_gates=*/true);
            codac2::SlicedTube<codac2::IntervalVector> tube(tdomain, init_box);

            // Pin the endpoint gates to the current solver bounds
            tube.set(X0, 0.);
            tube.set(Xt, t_ub);

            // FWD_BWD narrows both endpoints jointly: the FWD pass narrows Xt
            // from X0, the BWD pass narrows X0 from Xt.
            cache->ctc.contract(tube, codac2::TimePropag::FWD_BWD);

            if (tube.is_empty()) return result;  // infeasible

            // Read narrowed endpoint gates back
            const codac2::IntervalVector& narrowed_X0 = tube.first_slice()->codomain();
            const codac2::IntervalVector& narrowed_Xt  = tube.last_slice()->codomain();

            result.found = true;
            result.t_new_lb = 0.0;
            result.t_new_ub = t_ub;

            result.vars_t_narrowed.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                result.vars_t_narrowed.emplace_back(
                    narrowed_Xt[i].lb(), narrowed_Xt[i].ub());

            result.vars_0_narrowed.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                result.vars_0_narrowed.emplace_back(
                    narrowed_X0[i].lb(), narrowed_X0[i].ub());

        } catch (const codac2::GlobalEnclosureError&) {
            // Integration failed to find a global enclosure; return conservatively
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // Trace: collect all enclosures for visualization
    // -------------------------------------------------------------------------

    CodacTraceResult run_lohner_trace(
        const std::shared_ptr<CodacOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        double t_ub,
        bool forward,
        int n_steps)
    {
        CodacTraceResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps <= 0) return result;

        const double h = t_ub / n_steps;
        if (h <= 0.0) return result;

        codac2::IntervalVector u0(n);
        for (int i = 0; i < n; ++i)
            u0[i] = codac2::Interval(u0_bounds[static_cast<std::size_t>(i)].first,
                                     u0_bounds[static_cast<std::size_t>(i)].second);

        try {
            codac2::LohnerAlgorithm algo(&cache->fn, h, forward, u0);

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
