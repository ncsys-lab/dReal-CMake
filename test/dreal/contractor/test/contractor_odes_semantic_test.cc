// Semantic correctness tests for the ODE contractor (Codac CtcLohner backend).
//
// The companion file `contractor_capd_test.cc` checks mechanical invariants
// (input/output bit masks, non-empty box) but never verifies that the
// narrowed intervals match the system's true reachable set. These tests
// close that gap by exercising `mk_contractor_ode_lohner` on systems with
// known closed-form solutions.
//
// Two gates per system:
//   - SOUNDNESS (must hold): on a SAT-known (X_0, X_t, t) instance, the box
//     stays non-empty after Prune(). A false UNSAT here is a soundness bug.
//   - VALUE (informational): on a UNSAT-known instance, a sound and
//     reasonably tight contractor should empty the box. Looseness here is
//     acceptable; a false SAT is not a bug per se, just an unhelpful
//     contractor.
//
// HEAD's BWD path is a no-op (`contractor_odes.cc` Step 4 early-return for
// BWD), so on HEAD the BWD soundness gates pass vacuously (box unchanged).
// The LohnerAlgorithm BWD experiment activates the BWD path; soundness
// gates must continue to pass with the experiment active — a false UNSAT
// (box becomes empty on a SAT-known case) is the abort condition.
//
// Note on value gates: the contractor does NOT mark the box empty when a
// component's narrowing intersection is empty (`contractor_odes.cc:315`
// skips the update). Infeasibility detection happens at the ICP/SAT layer,
// not in the contractor itself. The TrivialFlowTest cases empty the box
// because they hit the trivial-flow short-circuit, which calls
// `set_empty()` explicitly. For non-trivial flows we focus on soundness
// gates only; value detection is benchmark territory, not unit-test
// territory.

#include "dreal/contractor/odes/contractor_odes.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::make_shared;
using std::vector;

// =============================================================================
// Fixture 1: trivial flow dx/dt = 0.
// State variable does not change; integral constraint reduces to X_0 ∩ X_t.
// Exercises the trivial-flow short-circuit in contractor_odes.cc.
// =============================================================================

// Variables and OdeFlow are `inline static` — one instance per fixture class,
// shared across every test in the class. This was originally a workaround
// for a cache-key bug in `make_codac_ode_cache` (raw `OdeFlow*` keys,
// vulnerable to pointer reuse after destruction). That bug is now fixed —
// the cache holds a shared_ptr to the OdeFlow alongside the cache slot —
// but the inline-static pattern is kept because it's tidier and avoids
// unnecessary cache-map growth in the test binary.

class TrivialFlowTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"trivial_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"trivial_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"trivial_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"trivial_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "trivial",
      vector<std::pair<Variable, Expression>>{{x_, Expression{0.0}}});

  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }

  void SetBounds(double x_lb, double x_ub, double xt_lb, double xt_ub,
                 double t_ub) {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(x_lb, x_ub);
    box_[xt_] = Box::Interval(xt_lb, xt_ub);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// Ground truth: UNSAT (disjoint X_0 = [0,1] and X_t = [2,3]; x is constant).
TEST_F(TrivialFlowTest, FwdInfeasible_BoxEmpties) {
  SetBounds(0.0, 1.0, 2.0, 3.0, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  EXPECT_TRUE(cs.box().empty())
      << "trivial flow, disjoint X_0 = [0,1] / X_t = [2,3] (FWD) should empty";
}

TEST_F(TrivialFlowTest, BwdInfeasible_BoxEmpties) {
  SetBounds(0.0, 1.0, 2.0, 3.0, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  EXPECT_TRUE(cs.box().empty())
      << "trivial flow, disjoint X_0 / X_t (BWD) should empty via short-circuit";
}

// =============================================================================
// Fixture 2: linear decay dx/dt = -x.
// Closed form: x(t) = x_0 * exp(-t).
// =============================================================================

class DecayFlowTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"decay_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"decay_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"decay_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"decay_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "decay", vector<std::pair<Variable, Expression>>{{x_, -x_}});

  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }

  void SetBounds(double x0_lb, double x0_ub, double xt_lb, double xt_ub,
                 double t_ub) {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(x0_lb, x0_ub);
    box_[xt_] = Box::Interval(xt_lb, xt_ub);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// Ground truth: SAT. X_0 = [1,2], X_t = [0.3,0.8], t = 1.
// Closed form: x(1) ∈ [e^-1, 2e^-1] ≈ [0.368, 0.736] ⊂ [0.3, 0.8].
TEST_F(DecayFlowTest, FwdFeasible_BoxRemains) {
  SetBounds(1.0, 2.0, 0.3, 0.8, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  ASSERT_FALSE(cs.box().empty())
      << "decay feasible (FWD): box should remain non-empty [SOUNDNESS GATE]";
  // xt is narrowed by FWD to its overapprox of x(1); the closed-form
  // interval [0.368, 0.736] must remain contained in the narrowed xt.
  EXPECT_LE(cs.box()[xt_].lb(), std::exp(-1.0))
      << "FWD must not over-prune x_t below 1*e^-1";
  EXPECT_GE(cs.box()[xt_].ub(), 2.0 * std::exp(-1.0))
      << "FWD must not over-prune x_t above 2*e^-1";
}

// Ground truth: SAT. Same instance as FwdFeasible.
// On HEAD (BWD-skip): contractor is a no-op; box trivially non-empty.
// Under experiment: BWD narrows x_0 toward backward image. The backward
// image of [0.3, 0.8] at t=0 for dx/dt = -x is [0.3*e, 0.8*e] ≈
// [0.815, 2.174]. Intersected with X_0 = [1, 2] this gives [1, 2] (no
// narrowing). Soundness: x_0.ub() must remain ≥ 2 — any tighter would drop
// valid initial states.
TEST_F(DecayFlowTest, BwdFeasible_BoxRemains) {
  SetBounds(1.0, 2.0, 0.3, 0.8, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  ASSERT_FALSE(cs.box().empty())
      << "decay feasible (BWD): box should remain non-empty [SOUNDNESS GATE]";
  EXPECT_LE(cs.box()[x0_].lb(), 1.0)
      << "BWD must not over-prune x_0 above 1 (drops the true point x_0=1)";
  EXPECT_GE(cs.box()[x0_].ub(), 2.0)
      << "BWD must not over-prune x_0 below 2 (drops the true point x_0=2)";
}

// =============================================================================
// Fixture 3: coupled rational dynamics — mock-prostate.
// dx/dt = -x * (z / (z + 2)),  dz/dt = -z.
// z is decoupled exponential decay: z(t) = z_0 * exp(-t).
// x decreases monotonically while z > 0; no closed form for x but x(t) ≤ x_0.
// This is the load-bearing fixture — rational coupling is where prostate's
// suspected unsoundness might live.
// =============================================================================

class MockProstateTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"prostate_x", Variable::Type::CONTINUOUS};
  inline static const Variable z_{"prostate_z", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"prostate_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable z0_{"prostate_z_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"prostate_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable zt_{"prostate_z_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"prostate_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, z_, x0_, z0_, xt_, zt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "mock_prostate",
      vector<std::pair<Variable, Expression>>{
          {x_, -x_ * (z_ / (z_ + 2.0))},
          {z_, -z_},
      });

  Formula MakeIc() const {
    return integral(0.0, t0_, {x0_, z0_}, {xt_, zt_}, ode_);
  }

  void SetBounds(double x0_lb, double x0_ub, double z0_lb, double z0_ub,
                 double xt_lb, double xt_ub, double zt_lb, double zt_ub,
                 double t_ub) {
    box_[x_] = Box::Interval(0.01, 100.0);  // x > 0 to avoid singularity issues
    box_[z_] = Box::Interval(0.01, 100.0);
    box_[x0_] = Box::Interval(x0_lb, x0_ub);
    box_[z0_] = Box::Interval(z0_lb, z0_ub);
    box_[xt_] = Box::Interval(xt_lb, xt_ub);
    box_[zt_] = Box::Interval(zt_lb, zt_ub);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// Ground truth: SAT.
// X_0 = x∈[5,10], z∈[1,2]; X_t = x∈[0.5,10], z∈[0.2,0.9]; t = 1.
// z(1) ∈ [e^-1, 2e^-1] ≈ [0.368, 0.736] ⊂ [0.2, 0.9].
// x(1) is monotonically below x_0 (decay rate z/(z+2) ∈ [0, 0.5]); x(1) ⊂ [0.5, 10].
TEST_F(MockProstateTest, FwdFeasible_BoxRemains) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.5, 10.0, 0.2, 0.9, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  EXPECT_FALSE(cs.box().empty())
      << "mock-prostate feasible (FWD): box should remain non-empty "
         "[SOUNDNESS GATE for rational coupling]";
}

// Ground truth: SAT. Same instance as FwdFeasible.
TEST_F(MockProstateTest, BwdFeasible_BoxRemains) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.5, 10.0, 0.2, 0.9, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  EXPECT_FALSE(cs.box().empty())
      << "mock-prostate feasible (BWD): box should remain non-empty "
         "[SOUNDNESS GATE for rational coupling, BWD path]";
}

// Soundness probe: after BWD on the feasible instance, x_0 should still
// contain the singleton x_0 = 7.5, z_0 = 1.5 (a clearly-reachable starting
// point — the forward trajectory from (7.5, 1.5) at t=1 lands somewhere in
// (x ≈ 5, z ≈ 0.55), well inside X_t). If BWD prunes 7.5 out of x_0 or 1.5
// out of z_0 under the experiment, that's an unsound under-approximation
// of the backward image.
TEST_F(MockProstateTest, BwdFeasible_PreservesInteriorPoint) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.5, 10.0, 0.2, 0.9, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  ASSERT_FALSE(cs.box().empty())
      << "mock-prostate feasible (BWD): box should remain non-empty";
  EXPECT_TRUE(cs.box()[x0_].contains(7.5))
      << "BWD must not prune x_0 = 7.5 (interior point with feasible trajectory)";
  EXPECT_TRUE(cs.box()[z0_].contains(1.5))
      << "BWD must not prune z_0 = 1.5 (interior point with feasible trajectory)";
}

// =============================================================================
// Long-horizon soundness tests.
//
// Post-Codac elimination, CAPD is the sole backend — the previously-needed
// A/B "_Capd"-suffixed variants and gate-dispatch tests have been removed.
// What remains are long-horizon soundness gates: CAPD must keep SAT
// instances non-empty even on t_ub = 20 (decay) and t_ub = 6 (prostate).
// =============================================================================

// Long-horizon decay: x(t) = x_0 * e^-t, t_ub = 20.
// Closed form: x(20) ∈ [e^-20, 2*e^-20] ≈ [2.06e-9, 4.12e-9].
TEST_F(DecayFlowTest, FwdFeasible_LongHorizonSoundness) {
  SetBounds(1.0, 2.0, 0.0, 1.0e-7, 20.0);
  Config config;  // default gates → CAPD triggers (t_ub=20 > 5.0)
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  ASSERT_FALSE(cs.box().empty())
      << "long-horizon decay (FWD, default gates): SAT instance should "
         "remain non-empty [SOUNDNESS GATE for CAPD on t_ub=20]";
  // After narrowing, x_t must still contain the closed-form trajectory.
  EXPECT_LE(cs.box()[xt_].lb(), std::exp(-20.0))
      << "FWD over-pruned x_t below 1*e^-20";
  EXPECT_GE(cs.box()[xt_].ub(), 2.0 * std::exp(-20.0))
      << "FWD over-pruned x_t above 2*e^-20";
}

TEST_F(DecayFlowTest, BwdFeasible_LongHorizonSoundness) {
  SetBounds(1.0, 2.0, 0.0, 1.0e-7, 20.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  ASSERT_FALSE(cs.box().empty())
      << "long-horizon decay (BWD, default gates): SAT instance should remain "
         "non-empty [SOUNDNESS GATE for CAPD backward integration on t_ub=20]";
  EXPECT_LE(cs.box()[x0_].lb(), 1.0)
      << "BWD over-pruned x_0 above 1 (drops true x_0=1)";
  EXPECT_GE(cs.box()[x0_].ub(), 2.0)
      << "BWD over-pruned x_0 below 2 (drops true x_0=2)";
}

// Long-horizon mock-prostate: rational coupling, t_ub = 6 just over the
// default gate. Tests the load-bearing fixture (rational dynamics) on
// the CAPD backward path.
TEST_F(MockProstateTest, FwdFeasible_LongHorizonSoundness) {
  // z(6) ≈ [z0*e^-6, 2*z0*e^-6] ≈ [2.48e-3, 4.96e-3] ⊂ [0, 0.01]
  // x is monotonically decreasing → x(6) ∈ [some pos value, 10]
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.001, 10.0, 0.0, 0.01, 6.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  EXPECT_FALSE(cs.box().empty())
      << "long-horizon mock-prostate (FWD, default gates → CAPD): SAT instance "
         "should remain non-empty [SOUNDNESS GATE]";
}

TEST_F(MockProstateTest, BwdFeasible_LongHorizonSoundness) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.001, 10.0, 0.0, 0.01, 6.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  EXPECT_FALSE(cs.box().empty())
      << "long-horizon mock-prostate (BWD, default gates → CAPD): SAT instance "
         "should remain non-empty [SOUNDNESS GATE]";
}

// Trivial-flow short-circuit precedence: the d/dt[x]=0 short-circuit must
// fire BEFORE the CAPD gate dispatch, even when the gate would otherwise
// trigger CAPD. Box should empty via the short-circuit's set_empty(), not
// fall through to CAPD integration of a zero RHS (which works but is wasted
// effort and not the right code path).
TEST_F(TrivialFlowTest, LongHorizon_ShortCircuitFires) {
  // t_ub = 20 would gate CAPD by default; trivial flow must short-circuit.
  SetBounds(0.0, 1.0, 2.0, 3.0, 20.0);
  Config config;  // default gates
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  EXPECT_TRUE(cs.box().empty())
      << "trivial flow with t_ub=20 must short-circuit (disjoint X_0/X_t) "
         "regardless of CAPD gate threshold";
}

// =============================================================================
// Fixture 4: 6D decoupled decay.
// Each x_i obeys dx_i/dt = -x_i. Closed form: x_i(t) = x_i(0) * e^-t.
// Six dimensions stresses CAPD on a moderate-state-space flow.
// =============================================================================

class SixDimDecayTest : public ::testing::Test {
 protected:
  inline static const Variable x1_{"d6_x1", Variable::Type::CONTINUOUS};
  inline static const Variable x2_{"d6_x2", Variable::Type::CONTINUOUS};
  inline static const Variable x3_{"d6_x3", Variable::Type::CONTINUOUS};
  inline static const Variable x4_{"d6_x4", Variable::Type::CONTINUOUS};
  inline static const Variable x5_{"d6_x5", Variable::Type::CONTINUOUS};
  inline static const Variable x6_{"d6_x6", Variable::Type::CONTINUOUS};

  inline static const Variable x1_0_{"d6_x1_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x2_0_{"d6_x2_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x3_0_{"d6_x3_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x4_0_{"d6_x4_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x5_0_{"d6_x5_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x6_0_{"d6_x6_0_0", Variable::Type::CONTINUOUS};

  inline static const Variable x1_t_{"d6_x1_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x2_t_{"d6_x2_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x3_t_{"d6_x3_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x4_t_{"d6_x4_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x5_t_{"d6_x5_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x6_t_{"d6_x6_0_t", Variable::Type::CONTINUOUS};

  inline static const Variable t0_{"d6_time_0", Variable::Type::CONTINUOUS};

  Box box_{vector<Variable>{
      x1_, x2_, x3_, x4_, x5_, x6_,
      x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_,
      x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_,
      t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "decay6",
      vector<std::pair<Variable, Expression>>{
          {x1_, -x1_}, {x2_, -x2_}, {x3_, -x3_},
          {x4_, -x4_}, {x5_, -x5_}, {x6_, -x6_},
      });

  Formula MakeIc() const {
    return integral(0.0, t0_,
                    {x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_},
                    {x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_}, ode_);
  }

  void SetFeasible(double t_ub) {
    // X_0 = [1, 2]^6, X_t = [0, 1]^6 (contains the closed-form x_0 * e^-t for
    // t_ub in [1, 30] easily).
    for (const auto& v : {x1_, x2_, x3_, x4_, x5_, x6_})
      box_[v] = Box::Interval(-100.0, 100.0);
    for (const auto& v : {x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_})
      box_[v] = Box::Interval(1.0, 2.0);
    for (const auto& v : {x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_})
      box_[v] = Box::Interval(0.0, 1.0);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// 6 state vars at t_ub=1 — exercise CAPD on a moderate-dimensional flow.
TEST_F(SixDimDecayTest, FwdFeasible_HighDim) {
  SetFeasible(1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  ASSERT_FALSE(cs.box().empty())
      << "6D decay (FWD, ndim gate → CAPD): SAT instance should stay non-empty";
  // Each xi_t must still contain its closed-form trajectory [e^-1, 2e^-1].
  for (const auto& vt : {x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_}) {
    EXPECT_LE(cs.box()[vt].lb(), std::exp(-1.0))
        << "over-pruned " << vt;
    EXPECT_GE(cs.box()[vt].ub(), 2.0 * std::exp(-1.0))
        << "over-pruned " << vt;
  }
}

TEST_F(SixDimDecayTest, BwdFeasible_HighDim) {
  SetFeasible(1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  ctc.Prune(&cs);
  ASSERT_FALSE(cs.box().empty())
      << "6D decay (BWD, ndim gate → CAPD): SAT instance should stay non-empty";
  // Each xi_0 must still contain {1, 2} (no soundness regression).
  for (const auto& v0 : {x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_}) {
    EXPECT_LE(cs.box()[v0].lb(), 1.0) << "BWD over-pruned " << v0;
    EXPECT_GE(cs.box()[v0].ub(), 2.0) << "BWD over-pruned " << v0;
  }
}

}  // namespace
}  // namespace dreal
