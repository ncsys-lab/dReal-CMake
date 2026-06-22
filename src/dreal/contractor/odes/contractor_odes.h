//
// Created by Kunal Sheth on 9/2/25.
// Post-Codac elimination: CAPD is the sole ODE backend.
//

#ifndef DREAL4_CMAKE_CONTRACTOR_ODES_H
#define DREAL4_CMAKE_CONTRACTOR_ODES_H
#include <iosfwd>
#include <memory>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "dreal/contractor/odes/ode_types.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"
#include "dreal/contractor/contractor_cell.h"
#include "dreal/symbolic/hash.h"
#include "dreal/util/box.h"
#include "nlohmann/json.hpp"

namespace dreal
{
    class CapdOdeCache;   // opaque; defined in contractor_odes_capd.cc

    std::ostream& operator<<(std::ostream& out, ode_direction const& d);

    // ODE contractor using IBEX interval arithmetic + CAPD order-20 Taylor.
    //
    // Prune() runs the following steps in order:
    //   1. Parameter consistency: pars_0 ∩ pars_t (parameters are constant
    //      along the trajectory and must agree at t=0 and t=T).
    //   2. T=0 special case: X_0 ∩ X_t (initial and final states must agree
    //      when time horizon is zero).
    //   3. Trivial-flow short-circuit: if every RHS is the literal 0, just
    //      intersect X_0 ∩ X_t (no integration needed — variables are
    //      constant along the trajectory).
    //   4. ODE trajectory integration via CAPD's IOdeSolver (order 20) +
    //      ITimeMap (backward via the negated -f(x) map), then cav26's
    //      per-slice tube filter: the ForallT invariant is checked on EACH
    //      trajectory slice (not just the endpoint — interior violations would
    //      otherwise be missed), each terminal-eligible slice is intersected
    //      with the X_t gate, survivors are hulled to narrow X_t and time, and
    //      an empty survivor set refutes (set_empty). See contractor_odes.cc.
    //
    // Sound for ODE problems: CAPD's order-20 Taylor enclosure provides a
    // guaranteed over-approximation of all trajectories from X_0.
    class contractor_ode_lohner : public ContractorCell
    {
    public:
        contractor_ode_lohner(Box const& box, const ode_constraint& ctr,
                              ode_direction dir, Config const& config,
                              double timeout = 0.0);
        std::ostream& display(std::ostream& out) const override;

        // Generate a JSON trace of the ODE trajectory for visualization
        // (the `--visualize` flag). Uses CAPD step-by-step IOdeSolver
        // (one enclosure per slice).
        nlohmann::json generate_trace(ContractorStatus cs_copy);

        void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;

    private:
        ode_direction const m_dir;
        const ode_constraint m_ctr;
        double const m_timeout; // unit: msec; 0 = no timeout
        // State and parameter variables (direction-adjusted: m_vars_0 is
        // always "start of integration", m_vars_t is always "end").
        std::vector<Variable> m_vars_0;
        std::vector<Variable> m_vars_t;
        std::vector<Variable> m_pars_0;
        std::vector<Variable> m_pars_t;
        bool m_need_to_check_inv{false};
        std::vector<Contractor> m_inv_ctcs;
        // ODE state variables in flow.ode_list order (positional match with
        // m_vars_0/m_vars_t). Precomputed in the constructor so Prune() doesn't
        // rebuild it on every call.
        std::vector<Variable> m_ode_state_vars;
        // Cached capd::IMap (fwd + bwd via -f(x)) for this flow. Null if the
        // RHS contains an Expression kind we don't translate to CAPD's
        // string format, or the capd::IMap parser rejects the result. When
        // null, Prune() runs steps 1-4 only (no ODE narrowing).
        std::shared_ptr<CapdOdeCache> m_capd_cache;
    };

    std::vector<Formula> unroll_conjunctions(const Formula& f);

    // Given a collection of assertions, finds all Integral formulas and links
    // each with its associated ForallT invariants (matching by flow name and
    // variable scope). Returns a vector of (Integral, [ForallT]) pairs.
    template <typename FormulaCollection>
    std::vector<ode_constraint> link_integral_invariants(const FormulaCollection& assertions) {
        std::vector<std::pair<Formula, std::vector<Formula>>> result;

        std::vector<Formula> int_ctrs;
        std::vector<Formula> inv_ctrs;
        for (const auto& a : assertions) {
            for (const auto& f : unroll_conjunctions(a)) {
                if (is_integral(f)) int_ctrs.push_back(f);
                else if (is_forallT(f)) inv_ctrs.push_back(f);
                else if (is_negation(f) && f.include_ode()) {
                    DREAL_LOG_DEBUG("Inverted ODE constraints are currently ignored: {}", f);
                }
                else if (f.include_ode()) {
                    DREAL_LOG_DEBUG("Nested ODE constraint currently ignored: {}", f);
                }
            }
        }

        for (const auto& ic : int_ctrs) {
            std::vector<Formula> local_invs;
            Variables vars_t_in_ic;
            const auto* const icc = to_integral(ic);
            for (const auto& vec_t : icc->get_vec_t()) vars_t_in_ic.insert(vec_t.GetVariables());
            for (const auto& fc : inv_ctrs) {
                const auto* const fcc = to_forallT(fc);
                // Link ForallTConstraint fc with IntegralConstraint ic, if
                //    fc.flow == ic.flow
                //    vars(fc.inv) ⊆ ic.vars_t
                if (fcc->get_flow()->name == icc->get_flow()->name) {
                    const auto vars_in_fc = fcc->get_bound_vars();
                    bool const included = vars_t_in_ic.IsSupersetOf(vars_in_fc);
                    if (included) {
                        local_invs.push_back(fc);
                    }
                }
            }
            result.emplace_back(ic, local_invs);
        }
        return result;
    }
}

#endif //DREAL4_CMAKE_CONTRACTOR_ODES_H
