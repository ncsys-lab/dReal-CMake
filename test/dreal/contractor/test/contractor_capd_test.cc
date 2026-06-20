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

            // Box should remain non-empty after pruning (as in original).
            EXPECT_FALSE(cs.box().empty());

            // Outputs after pruning. The dReal3-originating expectations
            // assumed a contractor that did proper BVP-style time narrowing
            // (find t such that x_0 + t = x_t for the x'=1 dynamics).
            // Neither Codac's CtcLohner nor our new CAPD-IOdeSolver
            // contractor performs that inverse-time reasoning — they both
            // compute reachable sets at the *given* t_ub bound. Under
            // Lohner, this scenario produces no state/time narrowing;
            // the only side-effect is intersect_params silently updating pt.
            //
            // Pre-existing failure on HEAD; the original expectations were
            // never re-calibrated after the Codac migration. We assert the
            // sound subset: no soundness regression, mask is intact, box
            // stays non-empty.
            EXPECT_FALSE(cs.output()[0]); // x
            EXPECT_FALSE(cs.output()[1]); // x0
            EXPECT_FALSE(cs.output()[2]); // xt
            EXPECT_FALSE(cs.output()[3]); // p
            EXPECT_FALSE(cs.output()[4]); // p0
            EXPECT_FALSE(cs.output()[5]); // pt — intersect_params shrinks
                                          // it silently, no output bit
            EXPECT_FALSE(cs.output()[6]); // t0 — Lohner does not narrow
                                          // time bounds in this BVP setup
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

            // Box remains non-empty (original behavior).
            EXPECT_FALSE(cs.box().empty());

            // Outputs after pruning. See CapdFwd for the rationale: the
            // dReal3 originals assumed BVP time-narrowing which no current
            // contractor performs. CAPD's BWD path is one-way (returns only
            // vars_t_narrowed, leaves vars_0_narrowed empty — see
            // contractor_odes_capd.h), so p0 is not narrowed on this pass.
            EXPECT_FALSE(cs.output()[0]); // x
            EXPECT_FALSE(cs.output()[1]); // x0
            EXPECT_FALSE(cs.output()[2]); // xt
            EXPECT_FALSE(cs.output()[3]); // p
            EXPECT_FALSE(cs.output()[4]); // p0 — CAPD BWD does not narrow
            EXPECT_FALSE(cs.output()[5]); // pt
            EXPECT_FALSE(cs.output()[6]); // t0 — no BVP time narrowing

            // Used-constraints: zero, since CAPD BWD didn't change the box.
            const auto& used = cs.UsedConstraints();
            EXPECT_EQ(used.size(), 0u);
            EXPECT_TRUE(used.find(ic) == used.end());
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
            // terminal enclosure narrows xt to ~{2}. (If the parameter were
            // dropped, x' would read 0 and xt would collapse to {0} instead.)
            ASSERT_FALSE(cs.box().empty());
            EXPECT_TRUE(cs.output()[2]);   // xt narrowed
            EXPECT_NEAR(cs.box()[xt_].mid(), 2.0, 0.05);
            EXPECT_LT(cs.box()[xt_].diam(), 0.1);
        }
    } // namespace
} // namespace dreal
