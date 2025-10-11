// /*
//    Copyright 2017 Toyota Research Institute
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
// */
// #include <dreal/solver/sat_solver.h>
//
// #include "dreal/solver/filter_assertion.h"
//
// #include <gtest/gtest.h>
//
// #include "dreal/symbolic/symbolic.h"
//
// namespace dreal
// {
//     namespace
//     {
//         class SatSolverIntervalTest : public ::testing::Test
//         {
//         protected:
//             void SetUp() override {}
//             Config c;
//             SatSolver s{c};
//             PredicateNormalizer pn;
//
//             const Variable x_{"x", Variable::Type::CONTINUOUS};
//             const Variable y_{"y", Variable::Type::CONTINUOUS};
//             const Variable z_{"z", Variable::Type::CONTINUOUS};
//         };
//
//         TEST_F(SatSolverIntervalTest, Implication7) {
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {-3, +4}));
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {-4, +3}));
//             EXPECT_TRUE(s.CheckSat());
//             s.AddFormula(!s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +2}));
//             EXPECT_TRUE(s.CheckSat());
//         }
//
//         TEST_F(SatSolverIntervalTest, Implication6) {
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +4}));
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {-4, +2}));
//             EXPECT_TRUE(s.CheckSat());
//             s.AddFormula(!s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +2}));
//             EXPECT_FALSE(s.CheckSat());
//         }
//
//         TEST_F(SatSolverIntervalTest, Implication5) {
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {+0, +2}));
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {-2, -0}));
//             EXPECT_TRUE(s.CheckSat()); // todo: is this true?? (incl. vs. excl. 0)
//             s.AddFormula(!s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +2}));
//             EXPECT_FALSE(s.CheckSat());
//         }
//
//         TEST_F(SatSolverIntervalTest, Implication4) {
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, y_, {+0, +2}));
//             EXPECT_TRUE(s.CheckSat());
//             s.AddFormula(!s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +2}));
//             EXPECT_TRUE(s.CheckSat());
//         }
//
//         TEST_F(SatSolverIntervalTest, Implication3) {
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +2}));
//             EXPECT_TRUE(s.CheckSat());
//             s.AddFormula(!s.MakeSatIntervalVarWithClauses(pn, x_, {+0, +2}));
//             EXPECT_TRUE(s.CheckSat());
//         }
//
//         TEST_F(SatSolverIntervalTest, Implication2) {
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {+0, +2}));
//             EXPECT_TRUE(s.CheckSat());
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +2}));
//             EXPECT_TRUE(s.CheckSat());
//         }
//
//         TEST_F(SatSolverIntervalTest, Implication1) {
//             s.AddFormula(s.MakeSatIntervalVarWithClauses(pn, x_, {+0, +2}));
//             EXPECT_TRUE(s.CheckSat());
//             s.AddFormula(!s.MakeSatIntervalVarWithClauses(pn, x_, {-2, +2}));
//             EXPECT_FALSE(s.CheckSat());
//         }
//     } // namespace
// } // namespace dreal
