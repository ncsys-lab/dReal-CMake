//
// Created by Kunal Sheth on 9/2/25.
//
#ifdef DREAL4_CMAKE_CAPD_HELPERS_H
#error capd_helpers.h may only be included once.
#endif

#ifndef DREAL4_CMAKE_CAPD_HELPERS_H
#define DREAL4_CMAKE_CAPD_HELPERS_H

#include <unordered_set>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/logging.h"
#include "dreal/contractor/odes/to_capd_string.h"
#include "dreal/contractor/odes/ode_types.h"
#include "dreal/util/rounding_mode_guard.h"

using Rect2Set = capd::C0Rect2Set;

using std::cerr;
using std::chrono::steady_clock;
using std::cout;
using std::endl;
using std::exception;
using std::fixed;
using std::function;
using std::initializer_list;
using std::left;
using std::list;
using std::logic_error;
using std::make_shared;
using std::milli;
using std::min;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::right;
using std::runtime_error;
using std::setprecision;
using std::setw;
using std::shared_ptr;
using std::size_t;
using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::unordered_set;
using std::vector;

// template <>
// struct fmt::formatter<capd::IVector> : fmt::ostream_formatter
// {};

// template <typename A, capd::vectalg::__size_type B>
// struct fmt::formatter<capd::vectalg::Vector<A, B>> : fmt::ostream_formatter
// {};

// template <>
// struct fmt::formatter<capd::IMap> : fmt::ostream_formatter
// {};

// template <>
// struct fmt::formatter<capd::Interval> : fmt::ostream_formatter
// {};

namespace dreal
{
    using Rect2Set = capd::C0Rect2Set;

    ostream& output_trace(ostream& out, capd::interval const dt, capd::IVector const& v,
                          vector<Variable> const& vars) {
        thread_local static ostringstream ss;
        out << "T = ";
        ss << dt;
        out << left << setw(20) << ss.str();
        ss.str(string());
        for (capd::IVector::size_type i = 0; i < v.dimension(); ++i) {
            out << " " << vars[i] << " : ";
            ss << v[i];
            out << left << setw(27) << ss.str();
            ss.str(string());
        }
        return out;
    }

    /// Divide interval @p iv into n pieces and put them into @p ret if the
    /// width of @p iv is non-zero. Otherwise, put @p iv into @p ret.
    void split(capd::interval const& iv, int const n, vector<capd::interval>& ret) {
        assert(iv.leftBound() <= iv.rightBound());
        double lb = iv.leftBound();
        double const rb = iv.rightBound();
        if (lb < rb) {
            ret.reserve(n);
            double const width = rb - lb;
            double const step = width / n;
            for (int i = 0; (lb <= rb) && (i < n - 1); ++i) {
                ret.emplace_back(lb, std::min(lb + step, rb));
                assert(lb <= std::min(lb + step, rb));
                lb += step;
            }
            if (lb < rb) {
                ret.emplace_back(lb, rb);
            }
        }
        else {
            // lb == rb
            ret.push_back(iv);
        }
    }

    bool contain_nan(capd::IVector const& v) {
        for (capd::interval const& i : v) {
            if (std::isnan(i.leftBound()) || std::isnan(i.rightBound())) {
                // DREAL_LOG_INFO("NaN Found: ", v);
                return true;
            }
        }
        return false;
    }

    // Build CAPD string from integral constraint
    // example : "var:v_2_0, x_2_0;fun:(-9.8000000000000007+(-0.450000*v_2_0)), v_2_0;"
    string build_capd_string(const Formula& ic, const ode_direction dir) {
        const auto icc = to_integral(ic);
        // Collect _0 variables
        // vector<Enode*> const& pars_0 = ic.get_pars_0();
        // vector<Enode*> const& par_lhs_names = ic.get_par_lhs_names();
        // vector<pair<Enode*, Enode*>> const& odes = ic.get_odes();

        // Build Map
        ExpressionSubstitution subst_map;
        const auto& vec_0 = (dir == ode_direction::FWD) ? icc->get_vec_0() : icc->get_vec_t();
        const auto& vars_0 = (dir == ode_direction::FWD) ? icc->get_vars_0() : icc->get_vars_t();
        const auto& pars_0 = (dir == ode_direction::FWD) ? icc->get_pars_0() : icc->get_pars_t();
        const auto& odes = icc->get_flow()->ode_list;
        for (size_t i = 0; i < vec_0.size(); ++i) {
            const auto& from = odes[i].first;
            const auto& to = vec_0[i];
            subst_map.emplace(from, to);
        }

        // Call Subst, and collect strings
        vector<string> ode_strs;
        for (unsigned i = 0; i < vec_0.size(); i++) {
            if (icc->get_flow()->is_par(odes[i].first)) continue;
            DREAL_ASSERT(icc->get_flow()->is_var(odes[i].first));
            auto ode = odes[i].second.Substitute(subst_map);
            if (dir == ode_direction::BWD) ode = -ode;
            ode_strs.emplace_back(to_capd_string(ode));
        }

        string diff_var;
        string diff_par;
        string diff_fun;
        if (!vars_0.empty()) diff_var = "var:" + join(vars_0, ", ") + ";";
        if (!pars_0.empty()) diff_par = "par:" + join(pars_0, ", ") + ";";
        if (!ode_strs.empty()) diff_fun = "fun:" + join(ode_strs, ", ") + ";";

        DREAL_LOG_DEBUG(diff_var);
        DREAL_LOG_DEBUG(diff_par);
        DREAL_LOG_DEBUG(diff_fun);
        return diff_var + diff_par + diff_fun;
    }

    capd::IVector extract_ivector(Box const& b, vector<Variable> const& vars) {
        capd::IVector intvs(vars.size());
        for (unsigned i = 0; i < vars.size(); i++) {
            const auto& var = vars[i];
            ibex::Interval const& intv = b[var];
            intvs[i] = capd::interval(intv.lb(), intv.ub());
        }
        return intvs;
    }

    void extract_ivector(Box const& b, vector<Variable> const& vars, capd::IVector& intvs) {
        for (unsigned i = 0; i < vars.size(); i++) {
            const auto& var = vars[i];
            ibex::Interval const& intv = b[var];
            intvs[i] = capd::interval(intv.lb(), intv.ub());
        }
    }

    void update_box_with_ivector(Box& b, vector<Variable> const& vars, capd::IVector iv) {
        capd::IVector intvs(vars.size());
        for (unsigned i = 0; i < vars.size(); i++) {
            b[vars[i]] = ibex::Interval(iv[i].leftBound(), iv[i].rightBound());
        }
    }

    capd::DVector extract_dvector(Box const& b, vector<Variable> const& vars) {
        RoundingModeGuard(FE_UPWARD);
        // Extract the b.mid() and return a capd::DVector
        capd::DVector intvs(vars.size());
        for (unsigned i = 0; i < vars.size(); i++) {
            const auto& var = vars[i];
            ibex::Interval const& intv = b[var];
            intvs[i] = intv.mid();
        }
        return intvs;
    }

    void extract_dvector(Box const& b, vector<Variable> const& vars, capd::DVector& intvs) {
        RoundingModeGuard(FE_UPWARD);
        for (unsigned i = 0; i < vars.size(); i++) {
            const auto& var = vars[i];
            ibex::Interval const& intv = b[var];
            intvs[i] = intv.mid();
        }
    }

    void update_box_with_dvector(Box b, vector<Variable> const& vars, capd::DVector iv) {
        capd::DVector intvs(vars.size());
        for (unsigned i = 0; i < vars.size(); i++) {
            b[vars[i]] = iv[i];
        }
    }

    bool filter(vector<pair<capd::interval, capd::IVector>>& enclosures, capd::IVector& X_t, capd::interval& T) {
        // 1) Intersect each v in enclosure with X_t.
        DREAL_LOG_DEBUG("filter : enclosure.size = ", enclosures.size());
        enclosures.erase(remove_if(enclosures.begin(), enclosures.end(),
                                   [&X_t](pair<capd::interval, capd::IVector>& item) {
                                       capd::IVector& v = item.second;
                                       // v = v union X_t
                                       // DREAL_LOG_DEBUG("before filter: ", v, "\t", X_t);
                                       if (!intersection(v, X_t, v)) {
                                           return true;
                                       }
                                       // DREAL_LOG_DEBUG("after filter: ", v);
                                       return false;
                                   }),
                         enclosures.end());
        if (enclosures.empty()) {
            return false;
        }
        // 2) If there is no intersection in 1), set dt an empty interval [0, 0]
        capd::interval all_T = enclosures.begin()->first;
        capd::IVector all_X_t = enclosures.begin()->second;
        for (pair<capd::interval, capd::IVector>& item : enclosures) {
            capd::interval& dt = item.first;
            capd::IVector& v = item.second;
            all_X_t = intervalHull(all_X_t, v);
            all_T = intervalHull(all_T, dt);
        }
        if (!intersection(T, all_T, T)) {
            return false;
        }
        if (!intersection(X_t, all_X_t, X_t)) {
            return false;
        }
        return true;
    }

    capd::IVector DV2IV(capd::DVector const& dv) {
        capd::IVector iv(dv.dimension());
        for (unsigned i = 0; i < iv.dimension(); ++i) {
            iv[i] = dv[i];
        }
        return iv;
    }

    Box intersect_params(Box& b, Formula const& ic) {
        const auto* const icc = to_integral(ic);
        const auto& pars_0 = icc->get_pars_0();
        const auto& pars_t = icc->get_pars_t();
        capd::IVector X_0 = extract_ivector(b, pars_0);
        capd::IVector const& X_t = extract_ivector(b, pars_t);
        if (!intersection(X_0, X_t, X_0)) {
            // intersection is empty
            b.set_empty();
        }
        else {
            // X_0 is the result of intersection of X_0 and X_t
            // So, use it to update pars_0 and pars_t
            update_box_with_ivector(b, icc->get_pars_0(), X_0);
            update_box_with_ivector(b, icc->get_pars_t(), X_0);
        }
        return b;
    }

    // template <typename T>
    // void set_params(T& f, Box const& b, Formula const& ic) {
    void set_params(capd::IMap& f, Box const& b, Formula const& ic) {
        const auto* const icc = to_integral(ic);
        const auto& pars_0 = icc->get_pars_0();
        capd::IVector X_0 = extract_ivector(b, pars_0);
        for (unsigned i = 0; i < pars_0.size(); i++) {
            string const& name = pars_0[i].get_name();
            f.setParameter(name, X_0[i]);
            // DREAL_LOG_DEBUG("set_param: ", name, " ==> ", X_0[i]);
        }
    }

    unsigned int extract_step(string const& name) {
        size_t last_pos_of_underscore = name.rfind('_');
        if (last_pos_of_underscore != string::npos) {
            size_t second_to_last_pos_of_underscore = name.rfind('_', last_pos_of_underscore - 1);
            if (second_to_last_pos_of_underscore != string::npos) {
                size_t l = last_pos_of_underscore - second_to_last_pos_of_underscore - 1;
                string step_part = name.substr(second_to_last_pos_of_underscore + 1, l);
                return stoi(step_part, nullptr);
            }
        }
        DREAL_LOG_CRITICAL("We found an error while generating a visualization for the ODE variable '", name, "'");
        DREAL_LOG_CRITICAL("This variable '", name, "' does not follow our convention for the visualization");
        DREAL_LOG_CRITICAL("We assume that an ODE varialbe is in a form of '<VAR_NAME>_<STEP>_{0,t}'");
        DREAL_LOG_CRITICAL("where <STEP> part should be an integer. An example is 'height_3_t'.");
        throw runtime_error("extract_step: Variable name convention");
    }
}

#endif //DREAL4_CMAKE_CAPD_HELPERS_H
