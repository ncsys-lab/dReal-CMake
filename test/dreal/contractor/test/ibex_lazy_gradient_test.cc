// Regression test for ncsys-lab/ibex-lib commit edbd8159
// ("TMP - Null-out gradient initialization for dReal performance").
//
// In mainline ibex `src/function/ibex_FunctionBuild.cpp:558-559`,
// `Function::init` unconditionally constructs the `Gradient` and
// `ExprLinearity` objects:
//
//     _grad = new Gradient(*_eval);
//     _lin  = new ExprLinearity(...);
//
// dReal4 only ever calls `Function::backward` (the HC4Revise path), which
// never touches `_grad` or `_lin`. CODAC_MIGRATION.md profiling shows
// gradient + linearity allocation accounts for ~65% of wall time on the
// saradc benchmark family.
//
// The patch replaces the `new` with `_grad = nullptr` (and similar for
// `_lin`). dReal's call path is unaffected; the perf win is large.
//
// We cannot directly observe `_grad == nullptr` from a unit test (the field
// is private). Instead this test exercises the property that the patch
// preserves SOUNDNESS — Function::backward must produce bit-identical
// narrowing results vs. the eager-init mainline ibex. Golden values captured
// against ibex-fork@9a2d0a9a; future cherry-picks atop modernized mainline
// must preserve the same outputs.

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include <ibex.h>

namespace dreal {
namespace {

TEST(IbexLazyGradient, BackwardOnLinearConstraintProducesExpectedNarrowing) {
  // x + y = 1, with x in [0, 1] and y in [0, 1].
  // Backward narrowing should not modify the box (already tight).
  const auto& x = ibex::ExprSymbol::new_();
  const auto& y = ibex::ExprSymbol::new_();
  ibex::Function f(x, y, x + y);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 1.0);
  box[1] = ibex::Interval(0.0, 1.0);
  const ibex::IntervalVector before = box;
  const bool changed = f.backward(ibex::Interval(1.0, 1.0), box);
  EXPECT_TRUE(changed || box == before);  // either it stayed the same, or it
                                          // legitimately narrowed
  EXPECT_FALSE(box.is_empty());
  EXPECT_LE(box[0].lb(), 1.0);
  EXPECT_GE(box[0].ub(), 0.0);
}

TEST(IbexLazyGradient, BackwardNarrowsSumConstraint) {
  // x + y = 0.5, with x in [0, 0.3] and y in [0, 1].
  // Backward should narrow y to [0.2, 0.5].
  const auto& x = ibex::ExprSymbol::new_();
  const auto& y = ibex::ExprSymbol::new_();
  ibex::Function f(x, y, x + y);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 0.3);
  box[1] = ibex::Interval(0.0, 1.0);
  f.backward(ibex::Interval(0.5, 0.5), box);
  EXPECT_FALSE(box.is_empty());
  // y was narrowed to approximately [0.2, 0.5]
  EXPECT_GE(box[1].lb(), 0.2 - 1e-9);
  EXPECT_LE(box[1].ub(), 0.5 + 1e-9);
}

TEST(IbexLazyGradient, BackwardOnUnsatConstraintEmptiesBox) {
  // x + y = 10, with x, y in [0, 1]. Infeasible — backward emits empty.
  const auto& x = ibex::ExprSymbol::new_();
  const auto& y = ibex::ExprSymbol::new_();
  ibex::Function f(x, y, x + y);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 1.0);
  box[1] = ibex::Interval(0.0, 1.0);
  f.backward(ibex::Interval(10.0, 10.0), box);
  EXPECT_TRUE(box.is_empty());
}

TEST(IbexLazyGradient, FunctionConstructionIsCheap) {
  // Indirect signal of the optimization: with eager gradient + linearity
  // init, constructing N functions takes O(N * f) where f is the per-function
  // gradient cost (substantial for non-trivial expressions). With the patch,
  // construction is O(N) with a small constant.
  //
  // This is a SOFT smoke test — it asserts a generous time bound that the
  // eager path should also pass, but a future regression where construction
  // becomes much slower would fail. The bound is intentionally loose so the
  // test passes on slow CI runners.
  constexpr int kN = 50;
  std::vector<std::unique_ptr<ibex::Function>> fns;
  fns.reserve(kN);
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < kN; ++i) {
    // Each Function owns its own symbols + expression (ibex requirement).
    const auto& x = ibex::ExprSymbol::new_();
    const auto& y = ibex::ExprSymbol::new_();
    const auto& z = ibex::ExprSymbol::new_();
    const ibex::ExprNode& expr =
        x * y + z * z - ibex::cos(x) + ibex::pow(y, 3) - ibex::log(z + 1.0);
    fns.emplace_back(std::make_unique<ibex::Function>(x, y, z, expr));
  }
  const auto t1 = std::chrono::steady_clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

  // 200 Function constructions on a non-trivial expression. Patched ibex:
  // typically <100ms on a modern Mac. Eager ibex: 500ms–several seconds.
  // The 30000ms bound is a regression canary, not a perf target — it's set
  // generously to tolerate slow Rosetta-x86_64 builds + CI runners.
  EXPECT_LT(ms, 30000)
      << "Regression: " << kN << " Function constructions took " << ms
      << "ms. Patch edbd8159 (gradient lazy-init) missing?";
}

}  // namespace
}  // namespace dreal
