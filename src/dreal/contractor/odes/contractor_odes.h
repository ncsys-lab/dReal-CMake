//
// Created by Kunal Sheth on 9/2/25.
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

#define nra_ODE_taylor_order 20
#define nra_ODE_grid_size 16
#define nra_ODE_absolute_tolerance 1e-20
#define nra_ODE_relative_tolerance 1e-20
#define nra_ODE_step 0.0
#define nra_ODE_show_progress true
#define nra_ODE_trace false

namespace std
{
    template <>
    struct hash<capd::Interval>
    {
        size_t operator()(const capd::Interval& v) const noexcept {
            size_t seed = 23;
            seed = dreal::drake::hash_combine<double>(seed, v.leftBound());
            seed = dreal::drake::hash_combine<double>(seed, v.rightBound());
            return seed;
        }
    };

    template <>
    struct equal_to<capd::Interval>
    {
        bool operator()(const capd::Interval& v1, const capd::Interval& v2) const {
            return v1.leftBound() == v2.leftBound() && v1.rightBound() == v2.rightBound();
        }
    };

    template <>
    struct hash<capd::IVector>
    {
        size_t operator()(const capd::IVector& v) const noexcept {
            size_t seed = 23;
            for (capd::Interval const& iv : v) {
                seed = dreal::drake::hash_combine<capd::Interval>(seed, iv);
            }
            return seed;
        }
    };

    template <>
    struct equal_to<capd::IVector>
    {
        bool operator()(const capd::IVector& v1, const capd::IVector& v2) const {
            if (v1.dimension() != v2.dimension()) return false;
            for (unsigned i = 0; i < v1.dimension(); i++) {
                if (v1[i] != v2[i]) return false;
            }
            return true;
        }
    };
} // namespace std

namespace dreal
{
    std::ostream& operator<<(std::ostream& out, ode_direction const& d);

    class contractor_capd_full : public ContractorCell
    {
    public:
        contractor_capd_full(Box const& box, const ode_constraint& ctr,
                             ode_direction const dir, Config const& config,
                             double const timeout = 0.0);
        std::ostream& display(std::ostream& out) const override;
        nlohmann::json generate_trace(ContractorStatus cs_copy);
        void Prune(ContractorStatus* cs) const override;

    private:
        ode_direction const m_dir;
        const ode_constraint m_ctr;
        unsigned long const m_taylor_order;
        unsigned long const m_grid_size;
        double const m_timeout; // unit: msec
        std::vector<Variable> m_vars_0;
        std::vector<Variable> m_vars_t;
        bool m_need_to_check_inv;
        std::vector<Contractor> m_inv_ctcs;
        std::unique_ptr<capd::IMap> m_vectorField;
        std::unique_ptr<capd::IOdeSolver> m_solver;
        std::unique_ptr<capd::ITimeMap> m_timeMap;

        bool inner_loop(capd::IOdeSolver& solver, capd::interval const& prevTime,
                        capd::interval const T,
                        std::vector<std::pair<capd::interval, capd::IVector>>& enclosures) const;
        bool check_invariant(capd::IVector const& v, ContractorStatus cs_copy) const;

        // template <typename Rect2Set>
        // bool check_invariant(Rect2Set const& rs, ContractorStatus* s) {
        //     thread_local static capd::IVector v;
        //     v = rs;
        //     return check_invariant(v, s);
        // }

        bool compute_enclosures(capd::interval const& prevTime, capd::interval const& T,
                                const ContractorStatus* s,
                                std::vector<std::pair<capd::interval, capd::IVector>>& enclosures,
                                bool const add_all = false) const;
    };

    std::vector<Formula> unroll_conjunctions(const Formula& f);

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
                    // from dReal3:
                    // TODO(soonhok): for now, a negation of forallt constraint is the same forallt constraint, but it will be ignored by contractor_capd4. Later, we will implement existt constraint and ODE contractors will support it
                }
                else if (f.include_ode()) {
                    DREAL_LOG_DEBUG("Nested ODE constraint currently ignored: {}", f);
                    // todo(c): idek what to do here... the semantics are so janky
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
                //    vars(fc.inv) \subseteq ic.vars_t
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
