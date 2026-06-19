//
// Created by Kunal Sheth on 9/2/25.
// Post-Codac elimination: CAPD is the sole ODE backend (order-10 Taylor).
//

#include "contractor_odes.h"
#include "contractor_odes_capd.h"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/solver/config.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounding_mode_guard.h"

namespace dreal
{
    using nlohmann::json;
    using std::vector;
    using std::pair;
    using std::string;
    using std::ostringstream;

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    // Parse the step number from a variable name of the form "<name>_<step>_{0,t}".
    // E.g. "height_3_t" -> 3.  Returns 0 on parse failure.
    static unsigned int extract_step(const std::string& name) {
        const size_t last = name.rfind('_');
        if (last != std::string::npos && last > 0) {
            const size_t second_last = name.rfind('_', last - 1);
            if (second_last != std::string::npos) {
                const std::string step_part = name.substr(second_last + 1, last - second_last - 1);
                try { return static_cast<unsigned int>(std::stoi(step_part)); }
                catch (...) {}
            }
        }
        return 0;
    }

    std::ostream& operator<<(std::ostream& out, ode_direction const& d) {
        switch (d) {
        case ode_direction::FWD:  out << "FWD"; break;
        case ode_direction::BWD:  out << "BWD"; break;
        }
        return out;
    }

    std::vector<Formula> unroll_conjunctions(const Formula& f) {
        if (is_conjunction(f)) {
            std::vector<Formula> result;
            for (const Formula& f_i : get_operands(f)) {
                const auto unrolled = unroll_conjunctions(f_i);
                result.insert(result.end(), unrolled.begin(), unrolled.end());
            }
            return result;
        }
        return {f};
    }

    // Intersect pars_0 and pars_t in-place.
    static bool intersect_params(Box& b, const FormulaIntegral* icc) {
        const auto& pars_0 = icc->get_pars_0();
        const auto& pars_t = icc->get_pars_t();
        for (size_t i = 0; i < pars_0.size(); ++i) {
            ibex::Interval& iv_0 = b[pars_0[i]];
            const ibex::Interval  iv_t = b[pars_t[i]];
            ibex::Interval intersected = iv_0 & iv_t;
            if (intersected.is_empty()) {
                b.set_empty();
                return false;
            }
            b[pars_0[i]] = intersected;
            b[pars_t[i]] = intersected;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // contractor_ode_lohner — constructor
    // ---------------------------------------------------------------------------

    contractor_ode_lohner::contractor_ode_lohner(
        Box const& box, const ode_constraint& ctr,
        ode_direction const dir, Config const& config,
        double const timeout)
        : ContractorCell{Contractor::Kind::ODE_LOHNER, DynamicBitset(box.size()), config},
          m_dir{dir},
          m_ctr{ctr},
          m_timeout{timeout}
    {
        const auto& ic = m_ctr.first;
        const auto* const icc = to_integral(ic);

        DynamicBitset& inp{mutable_input()};
        for (const auto& var : ic.GetFreeVariables()) inp.set(box.index(var));

        if (m_dir == ode_direction::FWD) {
            m_vars_0 = icc->get_vars_0();
            m_vars_t = icc->get_vars_t();
            m_pars_0 = icc->get_pars_0();
            m_pars_t = icc->get_pars_t();
        } else {
            m_vars_0 = icc->get_vars_t();
            m_vars_t = icc->get_vars_0();
            m_pars_0 = icc->get_pars_t();
            m_pars_t = icc->get_pars_0();
        }

        if (!m_ctr.second.empty()) {
            RoundingModeGuard g(FE_UPWARD);
            for (const auto& inv : m_ctr.second) {
                const auto* const invc = to_forallT(inv);
                if (is_conjunction(invc->get_bound_f())) {
                    std::vector<Contractor> ctcs;
                    for (const auto& nl_ctr : get_operands(invc->get_bound_f())) {
                        DREAL_LOG_INFO("Building ibex contractor for {} expr within {}", nl_ctr, inv);
                        ctcs.push_back(make_contractor_ibex_fwdbwd(nl_ctr, box, config));
                    }
                    m_inv_ctcs.push_back(make_contractor_seq(ctcs, config));
                } else {
                    DREAL_LOG_INFO("Building ibex contractor for {}", inv);
                    m_inv_ctcs.push_back(make_contractor_ibex_fwdbwd(invc->get_bound_f(), box, config));
                }
            }
            m_need_to_check_inv = true;
        }

        // Precompute the ode_state_vars ordering and build the CAPD IMap
        // cache once. Prune() reuses it on every call.
        m_ode_state_vars.reserve(icc->get_flow()->ode_list.size());
        for (const auto& [ode_var, _rhs] : icc->get_flow()->ode_list)
            m_ode_state_vars.push_back(ode_var);
        {
            RoundingModeGuard g(FE_TONEAREST);
            // CAPD's IMap parser is moderately expensive; doing it here
            // keeps Prune off the cold per-flow translation path. If the RHS
            // cannot be translated/parsed, make_capd_ode_cache *raises* (rather
            // than silently returning a no-op contractor); it returns null only
            // when there is no flow at all, which the Prune/trace null-checks
            // still guard.
            m_capd_cache = make_capd_ode_cache(icc->get_flow(), m_ode_state_vars);
        }
        (void)config;
    }

    // ---------------------------------------------------------------------------
    // Prune
    // ---------------------------------------------------------------------------

    void contractor_ode_lohner::Prune(ContractorStatus* cs, const UpwardRounding& ur) const {
        // Crossing the gaol->CAPD boundary: before we switch to FE_TONEAREST,
        // assert the incoming mode is still what the guard stack expects. A
        // failure here means something changed the FPU mode without a guard
        // (the classic CAPD-clobber / un-guarded-getter hazard). Debug-only.
        DREAL_ASSERT_ROUNDING_CONSISTENT();
        RoundingModeGuard g(FE_TONEAREST);
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);

        DREAL_LOG_DEBUG("contractor_ode_lohner::Prune [{} dir={}]",
                        m_ctr.first, m_dir == ode_direction::FWD ? "FWD" : "BWD");

        const auto& ic     = m_ctr.first;
        const auto* const icc = to_integral(ic);

        // --- Step 1: Intersect parameters (pars_0 ∩ pars_t) ---
        if (!intersect_params(cs->mutable_box(), icc)) {
            for (const auto& v : icc->get_pars_0()) cs->mutable_output().set(cs->box().index(v));
            for (const auto& v : icc->get_pars_t()) cs->mutable_output().set(cs->box().index(v));
            cs->AddUsedConstraint(ic);
            cs->AddUsedConstraint(m_ctr.second);
            return;
        }

        // --- Step 2: T=0 special case ---
        const auto& icct = icc->get_time_t();
        const bool time_is_zero =
            (is_variable(icct) && cs->box()[get_variable(icct)].ub() == 0.0) ||
            is_constant(icct, 0.0);

        if (time_is_zero) {
            const Box old_box = cs->box();
            for (size_t i = 0; i < m_vars_0.size(); ++i) {
                ibex::Interval& iv_0 = cs->mutable_box()[m_vars_0[i]];
                ibex::Interval& iv_t = cs->mutable_box()[m_vars_t[i]];
                iv_0 &= iv_t;
                if (iv_0.is_empty()) {
                    cs->mutable_box().set_empty();
                    cs->AddUsedConstraint(ic);
                    cs->AddUsedConstraint(m_ctr.second);
                    cs->mutable_output() |= input();
                    return;
                }
                iv_t = iv_0;
            }
            for (int i = 0; i < old_box.size(); ++i) {
                if (cs->box()[i] != old_box[i])
                    cs->mutable_output().set(static_cast<DynamicBitset::size_type>(i));
            }
            cs->AddUsedConstraint(ic);
            cs->AddUsedConstraint(m_ctr.second);
            return;
        }

        // --- Step 3: Invariant checking at X_0 endpoint ---
        if (m_need_to_check_inv) {
            const auto& invs = m_ctr.second;
            DREAL_ASSERT(invs.size() == m_inv_ctcs.size());
            ContractorStatus cs_0 = *cs;
            // We are inside the CAPD FE_TONEAREST guard, but the invariant
            // contractors are ibex/gaol and need FE_UPWARD. Re-establish it
            // with an UpwardRoundingScope, which also mints the token they
            // require — the token makes this reentrant mode switch mandatory
            // rather than easy-to-forget.
            const UpwardRoundingScope inv_scope;
            for (size_t i = 0; i < invs.size(); ++i) {
                if (!is_negation(invs[i])) {
                    m_inv_ctcs[i].Prune(&cs_0, inv_scope.token());
                    if (cs_0.box().empty()) {
                        DREAL_LOG_INFO("contractor_ode_lohner::Prune - invariant violated at X_0");
                        cs->mutable_box().set_empty();
                        cs->AddUsedConstraint(ic);
                        cs->AddUsedConstraint(m_ctr.second);
                        cs->mutable_output().set();
                        return;
                    }
                } else {
                    DREAL_LOG_WARN("contractor_ode_lohner::Prune - negated invariant ignored: {}", invs[i]);
                }
            }
        }

        // --- Step 4: ODE trajectory integration via CAPD order-10 Taylor ---
        //
        // Direction handling:
        //
        // FWD contractor (m_dir == FWD): m_vars_0 = original X_0,
        //   m_vars_t = original X_t. We call run_capd_fwd which forward-
        //   integrates f(x) from X_0 over [0, t_ub], intersects the
        //   terminal enclosure with X_t (→ vars_t_narrowed), then backward-
        //   integrates -f(x) from the narrowed terminal back to t=0 to
        //   recover the joint narrowing on X_0 (→ vars_0_narrowed).
        //
        // BWD contractor (m_dir == BWD): the constructor swapped variables
        //   so m_vars_0 = original X_t, m_vars_t = original X_0. We call
        //   run_capd_bwd, which integrates -f(x) from u0 (= original X_t)
        //   over [0, t_ub] using the negated IMap stored in the cache.
        //   The terminal enclosure is the backward image — the set of
        //   states at real time 0 whose forward trajectory under f(x)
        //   reaches the original X_t. Intersecting this with m_vars_t
        //   (= original X_0) is sound by construction.
        //
        // If CAPD diverges (step-control failure / over-approximation
        // explodes), the run returns found=false and we silently skip
        // narrowing on this pass. There is no other backend to fall back to.

        if (!m_capd_cache) return;  // RHS not translatable to capd::IMap
        if (!is_variable(icct)) return;
        const Variable time_var = get_variable(icct);
        const double t_ub = cs->box()[time_var].ub();
        if (t_ub <= 0.0) return;

        const int n = static_cast<int>(m_vars_0.size());

        // Trivial-flow short-circuit: every RHS is the literal constant 0, so
        // every state variable is constant along the trajectory. The original
        // constraint reduces to X_0 == X_t componentwise — identical to the
        // time_is_zero case handled above. Without this, CAPD's IOdeSolver
        // is called ~N_modes times per ICP step and dominates Prune cost on
        // the k1280 planning benchmark. Direction-agnostic.
        if (capd_ode_cache_is_trivial(m_capd_cache)) {
            const Box old_box = cs->box();
            for (size_t i = 0; i < m_vars_0.size(); ++i) {
                ibex::Interval& iv_0 = cs->mutable_box()[m_vars_0[i]];
                ibex::Interval& iv_t = cs->mutable_box()[m_vars_t[i]];
                iv_0 &= iv_t;
                if (iv_0.is_empty()) {
                    cs->mutable_box().set_empty();
                    cs->AddUsedConstraint(ic);
                    cs->AddUsedConstraint(m_ctr.second);
                    cs->mutable_output() |= input();
                    return;
                }
                iv_t = iv_0;
            }
            for (int i = 0; i < old_box.size(); ++i) {
                if (cs->box()[i] != old_box[i])
                    cs->mutable_output().set(static_cast<DynamicBitset::size_type>(i));
            }
            cs->AddUsedConstraint(ic);
            return;
        }

        // Initial condition: start-of-integration intervals.
        // FWD: m_vars_0 = original X_0 (start of forward integration)
        // BWD: m_vars_0 = original X_t (start of backward integration)
        std::vector<std::pair<double, double>> u0_bounds;
        u0_bounds.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const ibex::Interval& iv = cs->box()[m_vars_0[static_cast<size_t>(i)]];
            u0_bounds.emplace_back(iv.lb(), iv.ub());
        }

        // Prepare X_t bounds (used only in FWD; ignored in BWD).
        std::vector<std::pair<double, double>> X_t_bounds;
        if (m_dir == ode_direction::FWD) {
            X_t_bounds.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                const ibex::Interval& iv = cs->box()[m_vars_t[static_cast<size_t>(i)]];
                X_t_bounds.emplace_back(iv.lb(), iv.ub());
            }
        }

        // Flow parameters (d/dt == 0 vars): bound the CAPD map's par: section
        // to their current box intervals. Ordered to match the cache's
        // par_names (== ode_list parameter order == m_pars_0 order). Params are
        // constant along the flow, so m_pars_0's interval is a sound value.
        std::vector<std::pair<double, double>> par_bounds;
        par_bounds.reserve(m_pars_0.size());
        for (const auto& pvar : m_pars_0) {
            const ibex::Interval& iv = cs->box()[pvar];
            par_bounds.emplace_back(iv.lb(), iv.ub());
        }

        CapdOdeResult res;
        if (m_dir == ode_direction::FWD) {
            res = run_capd_fwd(m_capd_cache, u0_bounds, X_t_bounds, par_bounds, t_ub);
        } else {
            res = run_capd_bwd(m_capd_cache, u0_bounds, par_bounds, t_ub);
        }

        if (!res.found) return;

        bool changed = false;

        // Narrow m_vars_t intervals from ODE terminal enclosure
        for (int i = 0; i < n; ++i) {
            ibex::Interval old_iv = cs->box()[m_vars_t[static_cast<size_t>(i)]];
            ibex::Interval encl(res.vars_t_narrowed[static_cast<size_t>(i)].first,
                                res.vars_t_narrowed[static_cast<size_t>(i)].second);
            ibex::Interval narrowed = old_iv & encl;
            if (!narrowed.is_empty() && narrowed != old_iv) {
                cs->mutable_box()[m_vars_t[static_cast<size_t>(i)]] = narrowed;
                cs->mutable_output().set(cs->box().index(m_vars_t[static_cast<size_t>(i)]));
                changed = true;
            }
        }

        // Narrow m_vars_0 intervals from ODE initial enclosure (CtcLohner BWD
        // pass; populated only by the FWD path's run_lohner_integration).
        // The BWD contractor's run_lohner_bwd_oneshot leaves this empty.
        if (!res.vars_0_narrowed.empty()) {
            for (int i = 0; i < n; ++i) {
                ibex::Interval old_iv = cs->box()[m_vars_0[static_cast<size_t>(i)]];
                ibex::Interval encl(res.vars_0_narrowed[static_cast<size_t>(i)].first,
                                    res.vars_0_narrowed[static_cast<size_t>(i)].second);
                ibex::Interval narrowed = old_iv & encl;
                if (!narrowed.is_empty() && narrowed != old_iv) {
                    cs->mutable_box()[m_vars_0[static_cast<size_t>(i)]] = narrowed;
                    cs->mutable_output().set(cs->box().index(m_vars_0[static_cast<size_t>(i)]));
                    changed = true;
                }
            }
        }

        // Narrow time variable
        {
            ibex::Interval old_t = cs->box()[time_var];
            ibex::Interval narrowed_t = old_t & ibex::Interval(res.t_new_lb, res.t_new_ub);
            if (!narrowed_t.is_empty() && narrowed_t != old_t) {
                cs->mutable_box()[time_var] = narrowed_t;
                cs->mutable_output().set(cs->box().index(time_var));
                changed = true;
            }
        }

        if (changed) cs->AddUsedConstraint(ic);
    }

    // ---------------------------------------------------------------------------
    // generate_trace
    // ---------------------------------------------------------------------------

    json contractor_ode_lohner::generate_trace(ContractorStatus cs_copy) {
        // CAPD's interval integrator expects the FPU in round-to-nearest (its
        // DoubleRounding sets directed modes per-op and restores nearest).
        // Prune() establishes this at its top; generate_trace is a separate
        // entry point (the --visualize path) and must establish it too, rather
        // than relying on whatever mode the caller left the FPU in.
        RoundingModeGuard g(FE_TONEAREST);

        const auto& ic     = m_ctr.first;
        const auto* const icc = to_integral(ic);
        Box& b = cs_copy.mutable_box();

        // Intersect parameters before tracing.
        if (!intersect_params(b, icc)) return json::array();

        // Time variable.
        const Expression& time_expr = icc->get_time_t();
        if (!is_variable(time_expr)) return json::array();
        const Variable time_var = get_variable(time_expr);
        const double t_lb = b[time_var].lb();
        const double t_ub = b[time_var].ub();
        if (t_ub <= 0.0) return json::array();

        // Initial condition for integration (direction-adjusted: m_vars_0 is start).
        std::vector<std::pair<double, double>> u0_bounds;
        u0_bounds.reserve(m_vars_0.size());
        for (const auto& var : m_vars_0) {
            const ibex::Interval& iv = b[var];
            u0_bounds.emplace_back(iv.lb(), iv.ub());
        }

        if (!m_capd_cache) return json::array();

        // Flow parameters (d/dt == 0 vars), ordered to match the cache's
        // par_names; bound into the CAPD map's par: section before tracing.
        std::vector<std::pair<double, double>> par_bounds;
        par_bounds.reserve(m_pars_0.size());
        for (const auto& pvar : m_pars_0) {
            const ibex::Interval& iv = b[pvar];
            par_bounds.emplace_back(iv.lb(), iv.ub());
        }

        const bool forward = (m_dir == ode_direction::FWD);
        const CapdTraceResult trace = run_capd_trace(
            m_capd_cache, u0_bounds, par_bounds, t_ub, forward);

        if (trace.points.empty()) return json::array();

        json ret = json::array();
        const std::string& mode_name = icc->get_flow()->name;
        const size_t n = m_vars_0.size();

        // One JSON entry per ODE state variable.
        for (size_t i = 0; i < n; ++i) {
            const std::string name = m_vars_0[i].get_name();
            json entry;
            entry["key"]    = name;
            entry["mode"]   = mode_name;
            entry["step"]   = extract_step(name);
            entry["values"] = json::array();
            for (const auto& pt : trace.points) {
                json value;
                value["time"]      = {pt.t_lb, pt.t_ub};
                value["enclosure"] = {pt.var_enclosures[i].first,
                                      pt.var_enclosures[i].second};
                entry["values"].push_back(value);
            }
            ret.push_back(entry);
        }

        // One JSON entry per parameter variable: just start and end time points.
        for (const auto& var : m_pars_0) {
            const std::string name = var.get_name();
            const ibex::Interval& iv = b[var];
            json entry;
            entry["key"]    = name;
            entry["mode"]   = mode_name;
            entry["step"]   = extract_step(name);
            entry["values"] = json::array();
            json v_begin, v_end;
            v_begin["time"]      = {0.0, 0.0};
            v_begin["enclosure"] = {iv.lb(), iv.ub()};
            v_end["time"]        = {t_lb, t_ub};
            v_end["enclosure"]   = {iv.lb(), iv.ub()};
            entry["values"].push_back(v_begin);
            entry["values"].push_back(v_end);
            ret.push_back(entry);
        }

        return ret;
    }

    // ---------------------------------------------------------------------------
    // display
    // ---------------------------------------------------------------------------

    std::ostream& contractor_ode_lohner::display(std::ostream& out) const {
        out << "contractor_ode_lohner(" << m_dir << ", " << m_ctr.first << ")";
        return out;
    }

} // namespace dreal
