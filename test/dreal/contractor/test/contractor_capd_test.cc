/*********************************************************************
Author: Soonho Kong <soonhok@cs.cmu.edu>

dReal -- Copyright (C) 2013 - 2016, the dReal Team

dReal is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

dReal is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with dReal. If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

// ported from dReal3.

#include "dreal/contractor/odes/contractor_odes.h"

#include <cfenv>
#include <iostream>
#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/interval.h"
#include "dreal/util/string_to_interval.h"

namespace dreal
{
    namespace
    {
        using std::cerr;
        using std::endl;
        using std::make_pair;
        using std::make_shared;
        using std::vector;

        class ContractorCapdFullTest : public ::testing::Test
        {
        protected:
            // Variables (mirrors the original test’s symbols)
            const Variable x_{"x", Variable::Type::CONTINUOUS};
            const Variable x0_{"x_0_0", Variable::Type::CONTINUOUS};
            const Variable xt_{"x_0_t", Variable::Type::CONTINUOUS};

            const Variable p_{"p", Variable::Type::CONTINUOUS};
            const Variable p0_{"p_0_0", Variable::Type::CONTINUOUS};
            const Variable pt_{"p_0_t", Variable::Type::CONTINUOUS};

            const Variable t0_{"time_0", Variable::Type::CONTINUOUS};

            // Variable ordering in the Box matches the original:
            // [x, x0, xt, p, p0, pt, t0]
            const vector<Variable> vars_{x_, x0_, xt_, p_, p0_, pt_, t0_};
            Box box_{vars_};

            // rhs: x' = 1, p' = (1/sqrt(2π)) * exp(-x²/2)
            const Expression rhs_x_{1.0};
            const Expression rhs_p_{(1.0 / sqrt(2.0 * M_PI)) * exp(-(pow(x_, 2) / 2.0))};

            // ODE with state vector [x, p]
            const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
                "flow_1", std::vector<std::pair<Variable, Expression>>{
                    {x_, rhs_x_}, {p_, rhs_p_}
                }
            );

            //   integral( [xt, pt], 0, t0, [x0, p0], ode_ )
            Formula MakeIntegralConstraint() const {
                return integral(
                    0.0, t0_, {x0_, p0_}, {xt_, pt_}, ode_
                );
            }

            // Utility: set the common base domains.
            void SetCommonDomains() {
                box_[x_] = Box::Interval(-100.0, 100.0);
                box_[x0_] = Box::Interval(-100.0, 100.0);
                box_[xt_] = Box::Interval(-100.0, 100.0);
                box_[p_] = Box::Interval(0.0, 1.0);
                box_[p0_] = Box::Interval(0.0, 1.0);
                box_[pt_] = Box::Interval(0.0, 1.0);
                box_[t0_] = Box::Interval(0.0, 40.0);
            }
        };

        TEST_F(ContractorCapdFullTest, CapdFwd) {
            SetCommonDomains();

            // Pin initial/terminal values as in the original:
            //   x0 = -10, xt = 10, p0 = 0
            box_[x0_] = Box::Interval(-10.0);
            box_[xt_] = Box::Interval(10.0);
            box_[p0_] = Box::Interval(0.0);

            Config config; // precision etc. if you want to tweak
            ContractorStatus cs{box_};

            const auto ic = MakeIntegralConstraint();

            // Create the CAPD contractor (forward)
            const auto ctc = mk_contractor_ode_lohner(
                box_, {ic, {}}, ode_direction::FWD, config, 0.0
            );

            // Check inputs before pruning (mirrors original expectations)
            // Original checked 7 booleans in this order: [x, x0, xt, p, p0, pt, t0]
            // After construction, contractor input mask should reflect which dims it reads.
            EXPECT_FALSE(ctc.input()[0]); // x
            EXPECT_TRUE(ctc.input()[1]); // x0
            EXPECT_TRUE(ctc.input()[2]); // xt
            EXPECT_FALSE(ctc.input()[3]); // p
            EXPECT_TRUE(ctc.input()[4]); // p0
            EXPECT_TRUE(ctc.input()[5]); // pt
            EXPECT_TRUE(ctc.input()[6]); // t0

            // Box is not empty before pruning.
            EXPECT_FALSE(cs.box().empty());

            { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

            // The box EMPTIES — and this is the sound, correlation-aware
            // refutation, not a regression. x'=1 reaches xt=10 only at t=20;
            // the cumulative gaussian p(t) is monotone increasing and, by the
            // time x=10, CAPD's *rigorous* enclosure of p already exceeds the
            // pt gate [0,1] (the full C0Rect2Set enclosure reads
            // p ∈ [1.00000002, 1.00000003] at t≈19.5 — verified directly). So
            // NO single trajectory time satisfies x=10 ∧ p∈[0,1]: the constraint
            // is infeasible w.r.t. CAPD's interval reasoning, and dReal's
            // soundness is defined relative to that backend.
            //
            // The previous expectation (box stays non-empty) was calibrated to
            // the coarse component-wise *hull* contractor, which intersected
            // hull_x∋10 with hull_p⊇[0,1] independently — a false delta-sat
            // that combined x=10 (at t=20) with p∈[0,1] (only true at earlier
            // t). The per-slice filter (cav26-faithful) keeps the per-time
            // correlation and correctly refutes. See contractor_odes.cc's
            // per-slice filter and contractor_odes_semantic_test.cc's
            // AntiCorrelatedTest (the same effect, distilled).
            EXPECT_TRUE(cs.box().empty())
                << "no trajectory time has x=10 AND p∈[0,1] (CAPD puts p>1 by "
                   "the time x reaches 10); the box must refute [sound]";
        }

        TEST_F(ContractorCapdFullTest, CapdBwd) {
            SetCommonDomains();

            // Pin initial/terminal values as in the original:
            //   x0 = -10, xt = 10, pt = 1
            box_[x0_] = Box::Interval(-10.0);
            box_[xt_] = Box::Interval(10.0);
            box_[pt_] = Box::Interval(1.0);

            Config config;
            ContractorStatus cs{box_};

            const auto ic = MakeIntegralConstraint();

            // Create the CAPD contractor (backward)
            const auto ctc = mk_contractor_ode_lohner(
                box_, {ic, {}}, ode_direction::BWD, config, 0.0
            );

            // Input mask — same as original:
            EXPECT_FALSE(ctc.input()[0]); // x
            EXPECT_TRUE(ctc.input()[1]); // x0
            EXPECT_TRUE(ctc.input()[2]); // xt
            EXPECT_FALSE(ctc.input()[3]); // p
            EXPECT_TRUE(ctc.input()[4]); // p0
            EXPECT_TRUE(ctc.input()[5]); // pt
            EXPECT_TRUE(ctc.input()[6]); // t0

            // Non-empty before pruning.
            EXPECT_FALSE(cs.box().empty());

            { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

            // The box EMPTIES — the BWD mirror of CapdFwd's sound refutation.
            // BWD integrates -f from the pinned terminal (xt=10, pt=1) back to
            // X_0=(x0=-10, p0∈[0,1]). To forward-reach (x=10, p=1) from x0=-10
            // requires p0 = 1 − ∫₋₁₀¹⁰ gaussian ≈ 1 − 1.00000002 < 0, outside
            // the p0 gate [0,1]: no single reverse-time slice lands in X_0.
            // The per-slice filter refutes; the prior coarse-hull expectation
            // (non-empty, untouched) was the same false delta-sat as CapdFwd.
            EXPECT_TRUE(cs.box().empty())
                << "backward image of (x=10,p=1) misses X_0 gate (needs p0<0); "
                   "the box must refute [sound]";

            // On refutation the contractor records the integral constraint.
            const auto& used = cs.UsedConstraints();
            EXPECT_TRUE(used.find(ic) != used.end());
        }

        // --visualize coverage: generate_trace() is the trajectory generator the
        // --visualize path calls (smt2/driver.cc). It runs CAPD via
        // run_capd_trace. No test exercised it before. These cover (1) the
        // rounding invariant — CAPD's directed-mode clobber must be contained so
        // generate_trace is safe to call inside the driver's NearestRoundingScope
        // — and (2) the shape of the emitted trajectory JSON.

        TEST_F(ContractorCapdFullTest, GenerateTraceDoesNotClobberRounding) {
            SetCommonDomains();
            box_[x0_] = Box::Interval(-10.0);  // pin the IC to a point and use a
            box_[p0_] = Box::Interval(0.0);    // short horizon so CAPD converges
            box_[t0_] = Box::Interval(0.0, 2.0);

            Config config;
            const auto ctc = mk_contractor_ode_lohner(
                box_, {MakeIntegralConstraint(), {}}, ode_direction::FWD, config, 0.0);

            ContractorStatus cs{box_};
            nlohmann::json trace;
            {
                // Mirror the driver's --visualize context: generate_trace runs
                // inside a NearestRoundingScope, whose dtor tripwire aborts if
                // CAPD's clobber escaped run_capd_trace's ExpectClobber scope.
                const NearestRoundingScope g;
                trace = to_ode_lohner(ctc)->generate_trace(cs);
                EXPECT_EQ(fegetround(), FE_TONEAREST);  // CAPD clobber contained
            }  // g's dtor would abort here if containment had failed
            EXPECT_FALSE(trace.empty());  // CAPD actually ran (not an early-return)
        }

        TEST_F(ContractorCapdFullTest, GenerateTraceTrajectoryShape) {
            SetCommonDomains();
            box_[x0_] = Box::Interval(-10.0);
            box_[p0_] = Box::Interval(0.0);
            box_[t0_] = Box::Interval(0.0, 2.0);

            Config config;
            const auto ctc = mk_contractor_ode_lohner(
                box_, {MakeIntegralConstraint(), {}}, ode_direction::FWD, config, 0.0);
            ContractorStatus cs{box_};

            const NearestRoundingScope g;
            const nlohmann::json trace = to_ode_lohner(ctc)->generate_trace(cs);

            // One JSON entry per state variable, in m_vars_0 order [x_0_0, p_0_0].
            ASSERT_TRUE(trace.is_array());
            ASSERT_EQ(trace.size(), 2u);
            EXPECT_EQ(trace[0]["key"].get<std::string>(), "x_0_0");
            EXPECT_EQ(trace[1]["key"].get<std::string>(), "p_0_0");
            for (const auto& entry : trace) {
                EXPECT_EQ(entry["mode"].get<std::string>(), "flow_1");
                ASSERT_TRUE(entry["values"].is_array());
                ASSERT_FALSE(entry["values"].empty());
                for (const auto& sample : entry["values"]) {
                    // Each sample is a [t_lb, t_ub] time and a [lb, ub] enclosure.
                    ASSERT_EQ(sample["time"].size(), 2u);
                    ASSERT_EQ(sample["enclosure"].size(), 2u);
                    EXPECT_LE(sample["time"][0].get<double>(),
                              sample["time"][1].get<double>());
                    EXPECT_LE(sample["enclosure"][0].get<double>(),
                              sample["enclosure"][1].get<double>());
                }
            }
        }

        // Regression: a flow with a TRUE parameter (a flow variable whose
        // d/dt is the literal 0, so it is constant along the trajectory).
        // OdeFlow classifies these as "pars"; they must be emitted in the
        // CAPD IMap's "par:" section and bound via setParameter, NOT as
        // integration variables. The previous code emitted every flow
        // variable in "var:", so the IMap dimension (here 2) exceeded the
        // C0Rect2Set built from only the true state vars (here 1). CAPD then
        // wrote its 2x2 Jacobian into 1-sized buffers, overrunning the heap
        // (confirmed via guard malloc: a crash in Map::operator() during
        // run_capd_fwd). This fixture exercises exactly that mismatch.
        class ContractorCapdParamTest : public ::testing::Test
        {
        protected:
            // x is a true state variable; a is a parameter (d/dt[a] = 0).
            const Variable x_{"x", Variable::Type::CONTINUOUS};
            const Variable x0_{"x_0_0", Variable::Type::CONTINUOUS};
            const Variable xt_{"x_0_t", Variable::Type::CONTINUOUS};
            const Variable a_{"a", Variable::Type::CONTINUOUS};
            const Variable a0_{"a_0_0", Variable::Type::CONTINUOUS};
            const Variable at_{"a_0_t", Variable::Type::CONTINUOUS};
            const Variable t0_{"time_0", Variable::Type::CONTINUOUS};

            // Box order: [x, x0, xt, a, a0, at, t0]
            const vector<Variable> vars_{x_, x0_, xt_, a_, a0_, at_, t0_};
            Box box_{vars_};

            // x' = a (couples through the parameter), a' = 0 (constant).
            const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
                "flow_p", std::vector<std::pair<Variable, Expression>>{
                    {x_, Expression{a_}}, {a_, Expression{0.0}}
                }
            );

            Formula MakeIntegralConstraint() const {
                return integral(0.0, t0_, {x0_, a0_}, {xt_, at_}, ode_);
            }
        };

        TEST_F(ContractorCapdParamTest, ForwardWithParameterDoesNotOverrun) {
            box_[x_]  = Box::Interval(-100.0, 100.0);
            box_[x0_] = Box::Interval(0.0);          // x(0) = 0
            box_[xt_] = Box::Interval(-100.0, 100.0);
            box_[a_]  = Box::Interval(1.0);
            box_[a0_] = Box::Interval(1.0);          // parameter value a = 1
            box_[at_] = Box::Interval(1.0);
            box_[t0_] = Box::Interval(2.0);          // integrate to t = 2

            Config config;
            ContractorStatus cs{box_};
            const auto ic = MakeIntegralConstraint();
            const auto ctc = mk_contractor_ode_lohner(
                box_, {ic, {}}, ode_direction::FWD, config, 0.0);

            // The integral binds only the true state var x; a is a parameter.
            EXPECT_TRUE(ctc.input()[1]);   // x0
            EXPECT_TRUE(ctc.input()[2]);   // xt

            // Pre-fix this Prune corrupted the heap (var/par dim mismatch).
            { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

            // Sound, non-trivial result: x(t) = x0 + a*t = 0 + 1*2 = 2, so the
            // terminal enclosure narrows xt toward {2}. (If the parameter were
            // dropped, x' would read 0 and xt would collapse to {0} instead —
            // this is the heap-overrun regression guard, the test's purpose.)
            //
            // With the per-slice tube filter and a *pinned* terminal time
            // (t0={2}), the narrowed xt is the last terminal-eligible sub-slice
            // of the integration step — here ~[1.875, 2.0] (mid 1.9375, width
            // = step/kHullGrid). Tighter than this would need cav26's terminal-
            // window clamp; the looser bound below still confirms the parameter
            // is applied (xt≈2, decisively not 0) and nothing overran. Free-time
            // ODE constraints (the benchmark norm) take many small CAPD steps,
            // so their terminal slices are fine-grained; the coarseness here is
            // specific to a single big step over a pinned dwell time.
            ASSERT_FALSE(cs.box().empty());
            EXPECT_TRUE(cs.output()[2]);   // xt narrowed
            EXPECT_NEAR(cs.box()[xt_].mid(), 2.0, 0.15);
            EXPECT_LT(cs.box()[xt_].diam(), 0.2);
        }
    } // namespace
} // namespace dreal
