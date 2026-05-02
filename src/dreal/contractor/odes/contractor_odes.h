//
// Created by Kunal Sheth on 9/2/25.
// Updated for Codac migration: removed CAPD dependency.
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
    std::ostream& operator<<(std::ostream& out, ode_direction const& d);

    // ODE contractor using IBEX interval arithmetic.
    //
    // This contractor handles:
    //   1. Parameter consistency: pars_0 ∩ pars_t (parameters are constant
    //      along the trajectory and must agree at t=0 and t=T).
    //   2. T=0 special case: X_0 ∩ X_t (initial and final states must agree
    //      when time horizon is zero).
    //   3. ForallT invariant checking at t=0 and t=T endpoints via IBEX HC4
    //      contractors.
    //
    // NOTE (Phase 4 TODO): Full ODE trajectory integration (rigorous enclosures
    // of all trajectories from X_0 to X_t over [0,T]) using Codac's CtcLohner
    // is not yet implemented. The contractor is sound (will not produce false
    // UNSAT results) but incomplete (may not detect infeasibility from ODE
    // dynamics alone). See CODAC_MIGRATION.md Phase 4.
    class contractor_ode_lohner : public ContractorCell
    {
    public:
        contractor_ode_lohner(Box const& box, const ode_constraint& ctr,
                              ode_direction dir, Config const& config,
                              double timeout = 0.0);
        std::ostream& display(std::ostream& out) const override;

        // Generate a JSON trace of the ODE trajectory for visualization.
        // NOTE: Returns an empty JSON object until trajectory integration is
        // implemented in Phase 4.
        nlohmann::json generate_trace(ContractorStatus cs_copy);

        void Prune(ContractorStatus* cs) const override;

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
