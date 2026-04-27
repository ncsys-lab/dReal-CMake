//
// Created by Kunal Sheth on 9/2/25.
// Updated for Codac migration: removed CAPD dependency.
//

#include "contractor_odes.h"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "dreal/contractor/contractor_ibex_fwdbwd.h"
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
    // Helpers (replacing capd_helpers.h)
    // ---------------------------------------------------------------------------

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

    // Compute the element-wise difference between two boxes of the same size.
    // Returns a bool vector: ret[i] = true iff b[i] != a[i].
    static std::vector<bool> diff_box(const Box& a, const Box& b) {
        assert(a.size() == b.size());
        std::vector<bool> ret(static_cast<size_t>(a.size()), false);
        for (int i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) ret[static_cast<size_t>(i)] = true;
        }
        return ret;
    }

    // Intersect pars_0 and pars_t in-place.  Parameters are constant along the
    // trajectory so their initial and final values must be equal.  Returns false
    // and sets the box to empty if any parameter pair has an empty intersection.
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

        // Mark all free variables of the integral formula as inputs.
        DynamicBitset& inp{mutable_input()};
        for (const auto& var : ic.GetFreeVariables()) inp.set(box.index(var));

        // Direction-adjusted state/param variables:
        //   m_vars_0 = start of integration, m_vars_t = end of integration
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

        // Build invariant contractors (if any).
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
    }

    // ---------------------------------------------------------------------------
    // Prune
    // ---------------------------------------------------------------------------

    void contractor_ode_lohner::Prune(ContractorStatus* cs) const {
        RoundingModeGuard g(FE_TONEAREST);

        // Save the old box to detect which variables were pruned.
        const Box old_box = cs->box();

        DREAL_LOG_DEBUG("contractor_ode_lohner::Prune [{} dir={}]",
                        m_ctr.first, m_dir == ode_direction::FWD ? "FWD" : "BWD");

        const auto& ic     = m_ctr.first;
        const auto* const icc = to_integral(ic);

        // --- Step 1: Intersect parameters (pars_0 ∩ pars_t) ---
        if (!intersect_params(cs->mutable_box(), icc)) {
            // Parameter domains are disjoint → UNSAT.
            for (const auto& v : icc->get_pars_0()) cs->mutable_output().set(cs->box().index(v));
            for (const auto& v : icc->get_pars_t()) cs->mutable_output().set(cs->box().index(v));
            cs->AddUsedConstraint(ic);
            cs->AddUsedConstraint(m_ctr.second);
            return;
        }

        // --- Step 2: T=0 special case — intersect X_0 and X_t ---
        const auto& icct = icc->get_time_t();
        const bool time_is_zero =
            (is_variable(icct) && cs->box()[get_variable(icct)].ub() == 0.0) ||
            is_constant(icct, 0.0);

        if (time_is_zero) {
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
            // Record what changed.
            auto diff = diff_box(old_box, cs->box());
            for (size_t i = 0; i < diff.size(); ++i) {
                if (diff[i]) cs->mutable_output().set(static_cast<DynamicBitset::size_type>(i));
            }
            if (!diff.empty()) {
                cs->AddUsedConstraint(ic);
                cs->AddUsedConstraint(m_ctr.second);
            }
            return;
        }

        // --- Step 3: General case (T > 0) ---
        //
        // Check invariants at the *current* X_0 and X_t endpoint enclosures.
        // This is sound: if the invariant is unsatisfiable at an endpoint, no
        // valid trajectory can pass through it.
        //
        // TODO (Phase 4): Implement full ODE trajectory integration using
        // Codac's CtcLohner.  This requires converting the symbolic ODE vector
        // field into a codac::AnalyticFunction<VectorType> and using a
        // codac::SlicedTube<ibex::IntervalVector> to represent the trajectory
        // enclosure.  See CODAC_MIGRATION.md §4 for the detailed plan.
        if (m_need_to_check_inv) {
            const auto& invs = m_ctr.second;
            DREAL_ASSERT(invs.size() == m_inv_ctcs.size());

            // Check invariant at X_0.
            {
                ContractorStatus cs_0 = *cs;
                // Alias X_0 vars into the contractor-status box so the ibex
                // invariant contractor sees the right intervals.
                RoundingModeGuard g_up(FE_UPWARD);
                for (size_t i = 0; i < invs.size(); ++i) {
                    if (!is_negation(invs[i])) {
                        m_inv_ctcs[i].Prune(&cs_0);
                        if (cs_0.box().empty()) {
                            DREAL_LOG_INFO("contractor_ode_lohner::Prune - invariant violated at X_0");
                            cs->mutable_box().set_empty();
                            cs->AddUsedConstraint(ic);
                            cs->AddUsedConstraint(m_ctr.second);
                            cs->mutable_output().set();
                            return;
                        }
                    } else {
                        DREAL_LOG_WARN("contractor_ode_lohner::Prune - Silent omission of negated invariant: {}", invs[i]);
                    }
                }
            }
        }

        // Record any changes from parameter intersection (the only pruning
        // this contractor currently performs in the T>0 general case).
        auto diff = diff_box(old_box, cs->box());
        bool changed = false;
        for (size_t i = 0; i < diff.size(); ++i) {
            if (diff[i]) {
                cs->mutable_output().set(static_cast<DynamicBitset::size_type>(i));
                changed = true;
            }
        }
        if (changed) {
            cs->AddUsedConstraint(ic);
            for (size_t i = 0; i < m_ctr.second.size(); ++i) {
                if (!is_negation(m_ctr.second[i])) {
                    cs->AddUsedConstraint(m_ctr.second[i]);
                } else {
                    DREAL_LOG_WARN("contractor_ode_lohner::Prune - Silent omission of negated invariant: {}", m_ctr.second[i]);
                }
            }
        }
    }

    // ---------------------------------------------------------------------------
    // generate_trace
    // ---------------------------------------------------------------------------

    json contractor_ode_lohner::generate_trace(ContractorStatus /*cs_copy*/) {
        // TODO (Phase 4): Implement trajectory tracing using Codac's CtcLohner.
        // Return empty trace until trajectory integration is implemented.
        DREAL_LOG_WARN("contractor_ode_lohner::generate_trace - trajectory tracing not yet implemented (Phase 4 TODO)");
        return json::array();
    }

    // ---------------------------------------------------------------------------
    // display
    // ---------------------------------------------------------------------------

    std::ostream& contractor_ode_lohner::display(std::ostream& out) const {
        out << "contractor_ode_lohner(" << m_dir << ", " << m_ctr << ")";
        return out;
    }

} // namespace dreal
