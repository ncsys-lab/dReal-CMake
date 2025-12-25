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
#include <dreal/util/rounding_mode_guard.h>

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
            const auto ctc = mk_contractor_capd_full(
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

            ctc.Prune(&cs);

            // Box should remain non-empty after pruning (as in original).
            EXPECT_FALSE(cs.box().empty());

            // Outputs after pruning — original expected:
            //   [false, false, false, false, false, true, true]
            // i.e., only pt and t0 changed.
            EXPECT_FALSE(cs.output()[0]); // x
            EXPECT_FALSE(cs.output()[1]); // x0
            EXPECT_FALSE(cs.output()[2]); // xt
            EXPECT_FALSE(cs.output()[3]); // p
            EXPECT_FALSE(cs.output()[4]); // p0
            EXPECT_TRUE(cs.output()[5]); // pt
            EXPECT_TRUE(cs.output()[6]); // t0

            // Used-constraints: exactly one (the ODE constraint we created).
            const auto& used = cs.UsedConstraints();
            EXPECT_EQ(used.size(), 1u);
            EXPECT_TRUE(used.find(ic) != used.end());
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
            const auto ctc = mk_contractor_capd_full(
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

            ctc.Prune(&cs);

            // Box remains non-empty (original behavior).
            EXPECT_FALSE(cs.box().empty());

            // Outputs after pruning — original expected:
            //   [false, false, false, false, true, false, true]
            // i.e., p0 and t0 changed.
            EXPECT_FALSE(cs.output()[0]); // x
            EXPECT_FALSE(cs.output()[1]); // x0
            EXPECT_FALSE(cs.output()[2]); // xt
            EXPECT_FALSE(cs.output()[3]); // p
            EXPECT_TRUE(cs.output()[4]); // p0
            EXPECT_FALSE(cs.output()[5]); // pt
            EXPECT_TRUE(cs.output()[6]); // t0

            // Used-constraints: exactly one.
            const auto& used = cs.UsedConstraints();
            EXPECT_EQ(used.size(), 1u);
            EXPECT_TRUE(used.find(ic) != used.end());
        }
    } // namespace
} // namespace dreal
