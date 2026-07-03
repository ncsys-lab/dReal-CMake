// Phase-0 soundness net (bucket 4): end-to-end verdict tests guarding correct
// empty-box propagation through the full HC4 -> contractor -> ICP -> verdict
// chain. Modeled on gaol_directed_rounding_false_unsat_test.cc.
//
// The EmptyBoxException refactor (converting IBEX's HC4Revise empty-signal from
// a thrown exception to a return-status) changes how an emptied domain is
// reported out of the backward contractor. These black-box tests pin the two
// failure directions that change must never introduce:
//
//   * UNSAT-must-hold: a *missed* empty signal would let an infeasible box look
//     satisfiable -> false `delta-sat`. Each formula here is infeasible by a
//     margin far larger than delta, so the correct verdict is unequivocally
//     UNSAT regardless of delta-weakening.
//
//   * SAT-must-hold: a *spurious* empty signal would prune away a real witness
//     -> false `unsat` (the classic soundness break, e.g. the gaol
//     directed-rounding bug). Each formula here is satisfiable but forces heavy
//     HC4 contraction, so a contractor that emptied too eagerly would be caught.
//
// These must be green on the current (throw-based) code and stay green, with no
// assertion edits, after the throw -> return-status conversion.

#include "dreal/api/api.h"

#include <gtest/gtest.h>

#include "dreal/symbolic/symbolic.h"

namespace dreal {
namespace {

constexpr double kDelta = 0.001;

// --- UNSAT-must-hold (a missed empty would surface as false delta-sat) ---

// sin has range [-1, 1]; sin(x) == 2 is infeasible by a margin of 1 >> delta.
TEST(Hc4EmptyPropagationSoundness, SinOutOfRangeIsUnsat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Formula f{sin(x) == 2.0};
  EXPECT_FALSE(CheckSatisfiability(f, kDelta))
      << "sin(x) == 2 is infeasible (|sin - 2| >= 1 > delta); a delta-sat "
         "verdict here means an empty box was not propagated.";
}

// x*x >= 0, so x*x == -1 is infeasible by a margin of 1 >> delta.
TEST(Hc4EmptyPropagationSoundness, NegativeSquareIsUnsat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Formula f{x * x == -1.0};
  EXPECT_FALSE(CheckSatisfiability(f, kDelta))
      << "x*x == -1 is infeasible (x*x >= 0); a delta-sat verdict means a "
         "missed empty.";
}

// Sum of squares is >= 0, so x*x + y*y == -1 is infeasible by a margin of 1.
// (We use -1.0, not a sub-delta value like -0.001: an infeasibility margin <=
// delta could legitimately be delta-sat, so it would not be a sound UNSAT
// assertion.)
TEST(Hc4EmptyPropagationSoundness, NegativeSumOfSquaresIsUnsat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const Formula f{x * x + y * y == -1.0};
  EXPECT_FALSE(CheckSatisfiability(f, kDelta))
      << "x*x + y*y == -1 is infeasible (sum of squares >= 0); a delta-sat "
         "verdict means a missed empty.";
}

// --- SAT-must-hold (a spurious empty would surface as false unsat) ---

// x*x == 4 with x >= 0 contracts hard toward x == 2 but is satisfiable.
TEST(Hc4EmptyPropagationSoundness, SquareEqualsFourWithSignConstraintIsSat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Formula f{(x * x == 4.0) && (x >= 0.0)};
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "x*x == 4 with x >= 0 is satisfiable (x == 2); an unsat verdict means "
         "a real witness was spuriously pruned.";
}

// A coupled linear system with a unique solution x == y == 0.5: heavy mutual
// contraction, but satisfiable.
TEST(Hc4EmptyPropagationSoundness, CoupledLinearSystemIsSat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const Formula f{(x + y == 1.0) && (x - y == 0.0)};
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "x+y == 1 & x-y == 0 is satisfiable (x == y == 0.5); an unsat verdict "
         "means a spurious empty.";
}

// Near-boundary feasibility: x*x <= 4 with x >= 1.999 leaves a thin feasible
// sliver x in [1.999, 2]. Forces contraction right up to a boundary that must
// remain satisfiable.
TEST(Hc4EmptyPropagationSoundness, ThinNearBoundarySliverIsSat) {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Formula f{(x * x <= 4.0) && (x >= 1.999)};
  EXPECT_TRUE(CheckSatisfiability(f, kDelta))
      << "x*x <= 4 & x >= 1.999 has a feasible sliver near x == 2; an unsat "
         "verdict means the boundary witness was spuriously pruned.";
}

}  // namespace
}  // namespace dreal
