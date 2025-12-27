//
// Created by Kunal Sheth on 9/2/25.
//

#include "contractor_odes.h"

#include <memory>

#include "capd_helpers.h"
#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounding_mode_guard.h"

namespace dreal
{
#define nra_ODE_taylor_order 20
#define nra_ODE_grid_size 16
#define nra_ODE_absolute_tolerance 1e-20
#define nra_ODE_relative_tolerance 1e-20
#define nra_ODE_step 0.0
#define nra_ODE_show_progress false
#define nra_ODE_trace false

    using nlohmann::json;

    std::ostream& operator<<(std::ostream& out, ode_direction const& d) {
        switch (d) {
        case ode_direction::FWD:
            out << "FWD";
            break;
        case ode_direction::BWD:
            out << "BWD";
            break;
        }
        return out;
    }

    contractor_capd_full::contractor_capd_full(Box const& box, const ode_constraint& ctr,
                                               ode_direction const dir, Config const& config,
                                               double const timeout)
        : ContractorCell{Contractor::Kind::CAPD_FULL, DynamicBitset(box.size()), config},
          m_dir{dir},
          m_ctr{ctr},
          m_taylor_order{nra_ODE_taylor_order},
          m_grid_size{nra_ODE_grid_size},
          m_timeout{timeout} {
        const auto ic = m_ctr.first;
        const auto* const icc = to_integral(m_ctr.first);

        DynamicBitset& input{mutable_input()};
        for (const auto& var : ic.GetFreeVariables()) input.set(box.index(var));

        m_vars_0 = (m_dir == ode_direction::FWD) ? icc->get_vars_0() : icc->get_vars_t();
        m_vars_t = (m_dir == ode_direction::FWD) ? icc->get_vars_t() : icc->get_vars_0();
        string const capd_str = build_capd_string(ic, m_dir);

        if (capd_str.find("var:") != string::npos) {
            DREAL_LOG_INFO("contractor_capd_full: diff sys = " + capd_str);
            m_vectorField.reset(new capd::IMap(capd_str));
            m_solver.reset(new capd::IOdeSolver(*m_vectorField, m_taylor_order));
            m_timeMap.reset(new capd::ITimeMap(*m_solver));

            // Turn on - Stop after step
            m_timeMap->stopAfterStep(true);

            // Precision
            m_solver->setAbsoluteTolerance(nra_ODE_absolute_tolerance);
            m_solver->setRelativeTolerance(nra_ODE_relative_tolerance);
        }
        // Set up m_inv_ctcs for invariant checking
        if (!m_ctr.second.empty()) {
            RoundingModeGuard g(FE_UPWARD);
            std::vector<Contractor> inv_ctcs;
            for (const auto& inv : m_ctr.second) {
                const auto* const invc = to_forallT(inv);
                if (is_conjunction(invc->get_bound_f())) {
                    std::vector<Contractor> ctcs;
                    for (auto const& nl_ctr : get_operands(invc->get_bound_f())) {
                        DREAL_LOG_INFO("Building ibex contractor for {} expr within {}", nl_ctr, inv);
                        ctcs.push_back(make_contractor_ibex_fwdbwd(nl_ctr, box, config));
                    }
                    m_inv_ctcs.push_back(make_contractor_seq(ctcs, config));
                }
                else {
                    DREAL_LOG_INFO("Building ibex contractor for {}", inv);
                    m_inv_ctcs.push_back(make_contractor_ibex_fwdbwd(invc->get_bound_f(), box, config));
                }
            }
            m_need_to_check_inv = true;
        }
        else {
            m_need_to_check_inv = false;
        }
    }

    // Prune v using inv_ctc. box b is needed to use inv_ctc.
    // Retrun false if invariant is violated.
    bool contractor_capd_full::check_invariant(capd::IVector const& v, ContractorStatus cs) const {
        // 1. convert v into a box using b.
        update_box_with_ivector(cs.mutable_box(), m_vars_t, v);
        // 2. check the converted box b, with inv_ctc contractor
        RoundingModeGuard g(FE_UPWARD);
        auto const& invs = m_ctr.second;
        DREAL_ASSERT(invs.size() == m_inv_ctcs.size());
        for (unsigned i = 0; i < invs.size(); ++i) {
            const auto& inv_e = invs[i];
            if (!is_negation(inv_e)) {
                m_inv_ctcs[i].Prune(&cs);
                if (cs.box().empty()) return false;
            } else DREAL_LOG_WARN("contractor_capd_full::check_invariant - Silent omission of invariant: {}", inv_e);
        }
        // // 3. extract v' from the pruned box b'
        // //    if b' is empty, then it means invariant violation
        // extract_ivector(b, m_vars_t, v);
        return true;
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
        else return {f};
    }

    bool contractor_capd_full::compute_enclosures(
        capd::interval const& prevTime, capd::interval const& T, const ContractorStatus* const cs,
        vector<pair<capd::interval, capd::IVector>>& enclosures, bool const add_all) const {
        auto const stepMade = m_solver->getStep();
        auto const& curve = m_solver->getCurve();
        auto domain = capd::interval(0, 1) * stepMade;

        thread_local static vector<capd::interval> intvs;
        intvs.clear();
        if (!add_all) {
            double const new_domain_left = T.leftBound() - prevTime.rightBound();
            double const domain_right = domain.rightBound();
            if (new_domain_left > 0.0 && new_domain_left <= domain_right) {
                domain.setLeftBound(new_domain_left);
            }
        }
        split(domain, m_grid_size, intvs);
        enclosures.reserve(enclosures.size() + intvs.size());
        for (capd::interval const& subsetOfDomain : intvs) {
            // DREAL_LOG_INFO("compute_enclosures: subsetOfDomain = ", subsetOfDomain);
            capd::interval const dt = prevTime + subsetOfDomain;
            if (add_all || (T.leftBound() <= dt.leftBound()) || (T.leftBound() <= dt.rightBound())) {
                //           [       T         ]
                // --------------------------------
                // 1.  [  O   ]
                // 2.         [   O   ]
                // 3.   [   O   ]
                // 4.  [  O  ]
                // 5. [  X ]
                capd::IVector v = curve(subsetOfDomain);
                // DREAL_LOG_INFO("compute_enclosures:", dt, "\t", v);
                if (nra_ODE_trace || DREAL_LOG_DEBUG_ENABLED) {
                    output_trace(cerr, dt, v, to_integral(m_ctr.first)->get_vars_0()) << endl;
                }
                if (!m_need_to_check_inv || check_invariant(v, /*copy*/ *cs)) {
                    enclosures.emplace_back(dt, v);
                }
                else {
                    if (nra_ODE_show_progress) {
                        cout << " [INVARIANT VIOLATED] ";
                    }
                    return false;
                }
            }
        }
        return true;
    }

    double max_width(capd::IVector const& iv) {
        double ret = 0.0;
        for (capd::interval const& i : iv) {
            double const current_width = i.rightBound() - i.leftBound();
            if (current_width > ret) {
                ret = current_width;
            }
        }
        return ret;
    }

    double min_width(capd::IVector const& iv) {
        double ret = std::numeric_limits<double>::max();
        for (capd::interval const& i : iv) {
            double const current_width = i.rightBound() - i.leftBound();
            if (current_width < ret) {
                ret = current_width;
            }
        }
        return ret;
    }

    std::vector<bool> do_diff_dims(Box const& a, Box const& b) {
        assert(a.size() == b.size());
        std::vector<bool> ret(a.size(), false);
        for (unsigned i = 0; i < b.size(); i++) {
            if (a[i] != b[i]) {
                ret[i] = true;
            }
        }
        return ret;
    }

    bool is_bisectable_at(const Box& thiz, int const idx, double const precision) {
        assert(precision >= 0.0);
        // Enode * const var = (*m_vars)[idx];
        const auto& var = thiz.variable(idx);
        const auto& iv = thiz[idx];
        RoundingModeGuard g(FE_UPWARD); // iv.diam() corrupts FPU state.
        if (!iv.is_bisectable()) {
            if (!iv.is_unbounded() && !iv.is_degenerated() && iv.diam() > precision) {
                ostringstream ss;
                string const var_name = var.get_name();
                ss << setprecision(20);
                ss << "Warning: The width of interval " << var_name << " = " << iv
                    << " is larger than the required precision";
                if (precision > 0.0) {
                    ss << setprecision(6);
                    ss << " (" << precision << ")";
                }
                ss << " but is no longer bisectable in the platform-native floating-point "
                    "representation";
                ss << " (" << (CHAR_BIT * sizeof(double)) << "-bit).";
                DREAL_LOG_WARN(ss.str());
            }
            return false;
        }
        double const current_diam = iv.diam(); // .diam corrupts FPU env.
        RoundingModeGuard g2(FE_TONEAREST); // FILO object construction/destruction
        double const ith_precision = precision;
        if (var.get_type() == Variable::Type::INTEGER) {
            //     [       iv        ]
            //         [         ]
            //   ceil(lb)    floor(ub)
            if (ceil(iv.lb()) == floor(iv.ub())) {
                return false;
            }
        }
        return current_diam > ith_precision;
    }

    bool is_bisectable(const Box& thiz, double const precision) {
        assert(precision >= 0.0);
        for (int i = 0; i < thiz.size(); ++i) {
            if (is_bisectable_at(thiz, i, precision)) {
                return true;
            }
        }
        return false;
    }

    void contractor_capd_full::Prune(ContractorStatus* cs) const {
        RoundingModeGuard g(FE_TONEAREST);

        auto const start_time = std::chrono::steady_clock::now();

        thread_local static Box old_box(cs->mutable_box());
        old_box = cs->mutable_box();
        DREAL_LOG_DEBUG("contractor_capd_full::prune ", m_dir == ode_direction::FWD ? "FWD" : "BWD");
        const auto& ic = m_ctr.first;
        const auto* const icc = to_integral(ic);
        cs->mutable_box() = intersect_params(cs->mutable_box(), ic);
        if (cs->mutable_box().empty()) {
            for (const auto& e : icc->get_pars_0()) {
                cs->mutable_output().set(cs->mutable_box().index(e));
            }
            for (const auto& e : icc->get_pars_t()) {
                cs->mutable_output().set(cs->mutable_box().index(e));
            }
            cs->AddUsedConstraint(m_ctr.first);
            cs->AddUsedConstraint(m_ctr.second);
            return;
        }
        if (!m_solver) {
            // Trivial Case where there are only params and no real ODE vars.
            return;
        }

        // Special Case: Time = [0, 0]
        // Intersect X_0 and X_t and return

        // dReal3:
        //      if (cs->mutable_box()[get_variable(icc->get_time_t())].ub() == 0.0) {
        // Kunal Fix:
        const auto &icct = icc->get_time_t();
        if (
            (is_variable(icct) && cs->mutable_box()[get_variable(icct)].ub() == 0.0) ||
            is_constant(icct, 0.0)
        ) {

            for (unsigned i = 0; i < m_vars_0.size(); ++i) {
                auto& iv_0_i = cs->mutable_box()[m_vars_0[i]];
                auto& iv_t_i = cs->mutable_box()[m_vars_t[i]];
                iv_0_i &= iv_t_i;
                if (iv_0_i.is_empty()) {
                    cs->mutable_box().set_empty();
                    cs->AddUsedConstraint(m_ctr.first);
                    cs->AddUsedConstraint(m_ctr.second);
                    cs->mutable_output() |= input();
                    return;
                }
                else {
                    iv_t_i = iv_0_i;
                }
            }
            // Setup m_output and m_used_constraints for SAT case
            std::vector<bool> diff_dims = do_diff_dims(cs->mutable_box(), old_box);
            for (unsigned i = 0; i < diff_dims.size(); i++) {
                if (diff_dims[i]) {
                    cs->mutable_output().set(i);
                }
            }
            if (!diff_dims.empty()) {
                cs->AddUsedConstraint(m_ctr.first);
                cs->AddUsedConstraint(m_ctr.second);
            }
            return;
        }

        // General case: Time = [lb, ub] where ub > 0
        set_params(*m_vectorField, cs->mutable_box(), ic);
        try {
            if (nra_ODE_step > 0) {
                m_solver->setStep(nra_ODE_step);
            }
            capd::IVector X_0 = extract_ivector(cs->mutable_box(), m_vars_0);
            capd::IVector X_t = extract_ivector(cs->mutable_box(), m_vars_t);

            // dReal3 code:
            //          ibex::Interval const& ibex_T =
            //              is_constant(icc->get_time_t())
            //              cs->mutable_box()[get_variable(icc->get_time_t())];
            //          capd::interval T(ibex_T.lb(), ibex_T.ub());
            // Kunal's Fix:
            capd::interval T;
            const auto& icct = icc->get_time_t();
            if (is_variable(icct)) {
                const auto& iv = cs->box()[get_variable(icct)];
                T = capd::interval(iv.lb(), iv.ub());
            }
            else if (is_real_constant(icct)) T = capd::interval(get_lb_of_real_constant(icct), get_ub_of_real_constant(icct));
            else if (is_constant(icct)) T = capd::interval(get_constant_value(icct));
            else DREAL_UNREACHABLE();

            // DREAL_LOG_INFO("X_0 : ", X_0);
            // DREAL_LOG_INFO("X_t : ", X_t);
            // DREAL_LOG_INFO("T   : ", T);
            Rect2Set rs(X_0);
            (*m_timeMap)(0.0, rs); // Rewind to 0.0
            capd::interval prevTime(0.);
            vector<pair<capd::interval, capd::IVector>> enclosures;
            do {
                // Handle Timeout
                if (m_timeout > 0.0 && is_bisectable(cs->mutable_box(), config().precision())) {
                    auto const end_time = steady_clock::now();
                    auto const time_diff_in_msec =
                        std::chrono::duration<double, milli>(end_time - start_time).count();
                    DREAL_LOG_INFO("ODE TIME: ", time_diff_in_msec, " / ", m_timeout);
                    if (time_diff_in_msec > m_timeout) {
                        DREAL_LOG_CRITICAL("ODE TIMEOUT!"
                                           , "\t", time_diff_in_msec, "msec / ", m_timeout
                                           , "msec");
                        throw runtime_error("ODE TIMEOUT");
                    }
                }
                // Invariant Check
                capd::IVector rsv = rs;
                if (m_need_to_check_inv && !check_invariant(rsv, /*copy*/ *cs)) {
                    if (nra_ODE_show_progress) {
                        cout << " [INVARIANT VIOLATED] ";
                    }
                    break;
                }
                // Move s toward m_T.rightBound()
                // interruption_point();
                (*m_timeMap)(T.rightBound(), rs);
                if (contain_nan(rsv)) {
                    DREAL_LOG_CRITICAL("contractor_capd_full::prune - contains NaN");
                }
                if (T.leftBound() <= m_timeMap->getCurrentTime().rightBound()) {
                    //                     [     T      ]
                    // [     current Time     ]
                    bool invariantSatisfied = compute_enclosures(prevTime, T, cs, enclosures);
                    if (!invariantSatisfied) {
                        DREAL_LOG_INFO("contractor_capd_full::prune - invariant violated");
                        break;
                    }
                }
                else if (nra_ODE_trace || DREAL_LOG_DEBUG_ENABLED) {
                    output_trace(cerr, prevTime, rsv, icc->get_vars_0()) << endl;
                }
                prevTime = m_timeMap->getCurrentTime();
                if (nra_ODE_show_progress) {
                    if (!nra_ODE_trace) {
                        cout << "\33[2K\r";
                    }
                    cout << "ODE Progress "
                        << "[" << m_dir << "]"
                        << ":  Time = " << setw(10) << fixed << setprecision(5) << right
                        << prevTime.rightBound() << " / " << setw(7) << fixed << setprecision(2)
                        << left << T.rightBound() << " " << setw(4) << right
                        << int(prevTime.rightBound() / T.rightBound() * 100.0) << "%"
                        << "  ";
                    if (!enclosures.empty()) {
                        auto const& last_iv = enclosures.back().second;
                        cout << "|T| = [" << setprecision(16) << min_width(last_iv) << ", "
                            << setprecision(5) << max_width(last_iv) << "]"
                            << "\t ";
                    }
                    const auto [max_diam, max_diam_var] = cs->mutable_box().MaxDiam();
                    cout << "Box Width = " << setw(10) << fixed << setprecision(5)
                        << max_diam << " (var: " << cs->box().variable(max_diam_var) << ")\t";
                    cout.flush();
                    if (nra_ODE_trace) {
                        cerr << endl;
                    }
                }
            }
            while (!m_timeMap->completed());
            if (nra_ODE_show_progress) {
                cout << " [Done] " << endl;
            }
            if (enclosures.size() > 0 && filter(enclosures, X_t, T)) {
                // SAT
                update_box_with_ivector(cs->mutable_box(), m_vars_t, X_t);
                // TODO(soonhok): Here we still assume that time_0 = zero.
                if (is_variable(icct))
                    cs->mutable_box()[get_variable(icc->get_time_t())] = ibex::Interval(T.leftBound(), T.rightBound());
                DREAL_LOG_DEBUG("contractor_capd_full::prune: get non-empty set after filtering");
            }
            else {
                // UNSAT
                DREAL_LOG_DEBUG("contractor_capd_full::prune: get empty set after filtering");
                cs->mutable_box().set_empty();
            }
        }
        catch (std::logic_error& e) {
            if (nra_ODE_show_progress) {
                cout << " [LogicError]" << endl;
            }
            throw e; // contractor_exception(e.what());
        }
        catch (std::range_error& e) {
            if (nra_ODE_show_progress) {
                cout << " [RangeError]" << endl;
            }
            // throw e; // contractor_exception(e.what());
            DREAL_LOG_INFO("contractor_capd_full::prune - std::range_error");
        }
        catch (capd::intervals::IntervalError<double>& e) {
            if (nra_ODE_show_progress) {
                cout << " [IntervalError]" << endl;
            }
            throw e; // contractor_exception(e.what());
        }
        catch (capd::ISolverException& e) {
            if (nra_ODE_show_progress) {
                cout << " [ISolverException]" << endl;
            }
            // dReal3 actually just logs and then no-ops on contractor_exception.
            // throw e; // contractor_exception(e.what());
            DREAL_LOG_INFO("contractor_capd_full::prune - ISolverException");
        }
        catch (std::runtime_error& e) {
            if (nra_ODE_show_progress) {
                cout << " [RuntimeError]" << endl;
            }
            throw e; // contractor_exception(e.what());
        }

        vector<bool> diff_dims = do_diff_dims(cs->mutable_box(), old_box);
        for (unsigned i = 0; i < diff_dims.size(); i++) {
            if (diff_dims[i]) {
                cs->mutable_output().set(i);
            }
        }
        if (!diff_dims.empty()) {
            // Add integral constraint
            cs->AddUsedConstraint(m_ctr.first);
            // Add forallt constraint (but only the asserted ones)
            auto const& invs = m_ctr.second;
            for (unsigned i = 0; i < invs.size(); ++i) {
                const auto inv_e = invs[i];
                if (!is_negation(inv_e)) {
                    cs->AddUsedConstraint(invs[i]);
                }
                else DREAL_LOG_WARN("contractor_capd_full::prune - Silent omission of invariant: {}", inv_e);
            }
        }
        return;
    }

    json generate_trace_core(Formula const& ic, vector<Variable> const& vars_0,
                             vector<Variable> const& pars_0, Box const& b, capd::interval const& T,
                             vector<pair<capd::interval, capd::IVector>> const& enclosures) {
        const auto icc = to_integral(ic);
        unsigned i = 0;
        json ret = {};
        for (auto const& var : vars_0) {
            json entry;
            string const name = var.get_name();
            entry["key"] = name;
            entry["mode"] = icc->get_flow()->name;
            entry["step"] = extract_step(name);
            entry["values"] = {};
            for (auto const& p : enclosures) {
                json value;
                value["time"] = {p.first.leftBound(), p.first.rightBound()};
                value["enclosure"] = {p.second[i].leftBound(), p.second[i].rightBound()};
                entry["values"].push_back(value);
            }
            ret.push_back(entry);
            i++;
        }
        for (auto const& var : pars_0) {
            json entry;
            string const name = var.get_name();
            entry["key"] = name;
            entry["mode"] = icc->get_flow()->name;
            entry["step"] = extract_step(name);
            entry["values"] = {};
            json value_begin, value_end;
            value_begin["time"] = {0.0, 0.0};
            value_begin["enclosure"] = {b[var].lb(), b[var].ub()};
            entry["values"].push_back(value_begin);
            value_end["time"] = {T.leftBound(), T.rightBound()};
            value_end["enclosure"] = {b[var].lb(), b[var].ub()};
            entry["values"].push_back(value_end);
            ret.push_back(entry);
        }
        return ret;
    }

    json contractor_capd_full::generate_trace(ContractorStatus cs) {
        const auto& ic = m_ctr.first;
        const auto icc = to_integral(ic);

        // aliases
        Box& b = cs.mutable_box();

        b = intersect_params(b, ic);
        if (!m_solver) {
            // Trivial Case where there are only params and no real ODE vars.
            return {};
        }
        set_params(*m_vectorField, b, ic);
        try {
            if (nra_ODE_step > 0) {
                m_solver->setStep(nra_ODE_step);
            }
            const auto& vars_0 =
                (m_dir == ode_direction::FWD) ? icc->get_vars_0() : icc->get_vars_t();
            const auto& vars_t =
                (m_dir == ode_direction::FWD) ? icc->get_vars_t() : icc->get_vars_0();
            const auto& pars_0 =
                (m_dir == ode_direction::FWD) ? icc->get_pars_0() : icc->get_pars_t();
            capd::IVector X_0 = extract_ivector(b, vars_0);
            capd::IVector X_t = extract_ivector(b, vars_t);

            // dReal3 code:
            // ibex::Interval const& ibex_T = b[get_variable(icc->get_time_t())];
            // capd::interval T(ibex_T.lb(), ibex_T.ub());
            // Kunal's Fix:
            capd::interval T;
            const auto& icct = icc->get_time_t();
            if (is_variable(icct)) {
                const auto& iv = b[get_variable(icct)];
                T = capd::interval(iv.lb(), iv.ub());
            }
            else if (is_real_constant(icct)) T = capd::interval(get_lb_of_real_constant(icct), get_ub_of_real_constant(icct));
            else if (is_constant(icct)) T = capd::interval(get_constant_value(icct));
            else DREAL_UNREACHABLE();

            Rect2Set rs(X_0);
            (*m_timeMap)(0.0, rs); // Rewind to 0.0
            m_timeMap->stopAfterStep(true);
            capd::interval prevTime(0.0);
            // Convert enclosures to json
            vector<pair<capd::interval, capd::IVector>> enclosures;
            do {
                // Move s toward m_T.rightBound()
                (*m_timeMap)(T.rightBound(), rs);
                if (contain_nan(rs)) {
                    DREAL_LOG_INFO("contractor_capd_full::generate_trace - contain NaN");
                }
                bool invariantSatisfied = compute_enclosures(prevTime, T, &cs, enclosures, true);
                if (!invariantSatisfied) {
                    DREAL_LOG_INFO("contractor_capd_full::generate_trace - invariant violated");
                    break;
                }
                prevTime = m_timeMap->getCurrentTime();
            }
            while (!m_timeMap->completed());
            return generate_trace_core(ic, vars_0, pars_0, cs.box(), T, enclosures);
        }
        catch (capd::intervals::IntervalError<double>& e) {
            throw DREAL_RUNTIME_ERROR(e.what());
        }
        catch (capd::ISolverException& e) {
            throw DREAL_RUNTIME_ERROR(e.what());
        }
        catch (exception& e) {
            throw DREAL_RUNTIME_ERROR(e.what());
        }
    }

    std::ostream& contractor_capd_full::display(std::ostream& out) const {
        out << "contractor_capd_full(" << m_dir << ", " << m_ctr << ")";
        return out;
    }
}
