/*
   Copyright 2017 Toyota Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
// Phase-0 soundness net (bucket 5): randomized fuzz over single-variable
// constraints, asserting correct empty-propagation through
// ContractorIbexFwdbwd::Prune in both directions. The seed is fixed, so the
// test is deterministic (never flaky) yet sweeps many expression shapes.
//
//   * No-false-unsat: build a constraint expr == f(x*) for a known feasible
//     witness x* inside the box. A sound contractor must NEVER empty (x* stays
//     feasible). Any empty is a spurious-empty / false-unsat bug.
//
//   * No-missed-root-empty: build a constraint whose target lies provably
//     above the function's global range over the box. A sound contractor MUST
//     empty. Any survival is a missed-empty / false-sat bug.
//
// Must be green on the current (throw-based) code and stay green, with no
// assertion edits, after the EmptyBoxException -> return-status conversion.
#include "dreal/contractor/contractor_ibex_fwdbwd.h"

#include <cmath>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/interval.h"
#include "dreal/util/rounding.h"

namespace dreal {
namespace {

using std::vector;

// Run a single ContractorIbexFwdbwd::Prune of formula f over a one-variable box
// [lo, hi] and report whether the box was emptied.
bool PruneEmpties(const Variable& x, const Formula& f, double lo, double hi) {
  const vector<Variable> vars{{x}};
  Box box{vars};
  box[x] = Box::Interval(lo, hi);
  ContractorStatus cs{box};
  const ContractorIbexFwdbwd ctc{f, box, Config{}};
  {
    const UpwardRoundingScope rms_;
    ctc.Prune(&cs, rms_.token());
  }
  return cs.box().empty();
}

// No-false-unsat: a constraint that is feasible at a known interior witness x*
// must never be emptied by a single Prune. We widen the box to [x*-w, x*+w] so
// the sound interval enclosure of the expression is a fat interval comfortably
// containing the target f(x*), independent of floating-point rounding.
TEST(ContractorIbexFwdbwdFuzz, FeasibleWitnessNeverEmpties) {
  std::mt19937 rng{20240620u};
  std::uniform_real_distribution<double> coeff{-3.0, 3.0};
  std::uniform_real_distribution<double> xstar_dist{-2.0, 2.0};
  constexpr double kMargin = 0.75;
  constexpr int kN = 300;

  const Variable x{"x", Variable::Type::CONTINUOUS};
  for (int i = 0; i < kN; ++i) {
    const double xs = xstar_dist(rng);
    const double a = coeff(rng);
    const double b = coeff(rng);
    const double c = coeff(rng);
    const int family = i % 3;

    Formula f{Formula::True()};
    double target = 0.0;
    switch (family) {
      case 0:  // linear: a*x + b
        target = a * xs + b;
        f = (a * x + b == target);
        break;
      case 1:  // quadratic: a*x*x + b*x + c
        target = a * xs * xs + b * xs + c;
        f = (a * x * x + b * x + c == target);
        break;
      default:  // transcendental: a*sin(x) + b
        target = a * std::sin(xs) + b;
        f = (a * sin(x) + b == target);
        break;
    }

    const bool emptied = PruneEmpties(x, f, xs - kMargin, xs + kMargin);
    EXPECT_FALSE(emptied)
        << "Spurious empty (false-unsat) on a feasible witness. seed=20240620 "
           "i=" << i << " family=" << family << " x*=" << xs << " a=" << a
        << " b=" << b << " c=" << c << " target=" << target;
  }
}

// No-missed-root-empty: a*sin(x) + b has global range within [b-|a|, b+|a|].
// A target set provably above that range (here +100) is infeasible everywhere,
// so a single Prune MUST empty the box at the root intersection.
TEST(ContractorIbexFwdbwdFuzz, OutOfRangeTargetAlwaysEmpties) {
  std::mt19937 rng{20240621u};
  std::uniform_real_distribution<double> amp{1.0, 3.0};
  std::uniform_real_distribution<double> off{-2.0, 2.0};
  constexpr double kFarAbove = 100.0;
  constexpr int kN = 200;

  const Variable x{"x", Variable::Type::CONTINUOUS};
  for (int i = 0; i < kN; ++i) {
    const double a = amp(rng);
    const double b = off(rng);
    // a*sin(x) + b in [b - a, b + a] subset of [-5, 5] << 100.
    const Formula f{a * sin(x) + b == kFarAbove};

    const bool emptied = PruneEmpties(x, f, -10.0, 10.0);
    EXPECT_TRUE(emptied)
        << "Missed empty (false-sat): a*sin(x)+b == 100 is infeasible "
           "everywhere. seed=20240621 i=" << i << " a=" << a << " b=" << b;
  }
}

}  // namespace
}  // namespace dreal
