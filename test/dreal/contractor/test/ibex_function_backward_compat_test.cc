// Source-compatibility regression test for the defaulted-callback arg
// introduced by ncsys-lab/ibex-lib commit 4d61b841.
//
// dReal4 calls `f.backward(y, box)` (no callback arg) at
// `src/dreal/contractor/contractor_ibex_fwdbwd.cc:122`. The post-4d61b841
// signature is:
//
//     bool backward(const Domain& y, IntervalVector& x,
//                   const std::function<void(int, const Interval&,
//                                            const Interval&)>& callback =
//                       [](int, const Interval&, const Interval&) {}) const;
//
// The defaulted callback should preserve source-compat for the two-arg
// call. This file IS that test: if it compiles, the contract holds.
//
// We deliberately do NOT include the callback variant here — the
// `ibex_backward_callback_test.cc` file does that. Each file proves one
// side of the source-compat property in isolation.

#include <gtest/gtest.h>

#include <ibex.h>

namespace dreal {
namespace {

TEST(IbexFunctionBackwardCompat, TwoArgOverloadCompilesAndRuns) {
  const auto& xs = ibex::ExprSymbol::new_();
  const auto& ys = ibex::ExprSymbol::new_();
  ibex::Function f(xs, ys, xs + ys);

  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 1.0);
  box[1] = ibex::Interval(0.0, 1.0);

  // The point of this file: the TWO-ARG form must still compile. This is the
  // call signature used at dreal4-cmake's contractor_ibex_fwdbwd.cc:122.
  const bool result = f.backward(ibex::Interval(0.5, 0.5), box);
  (void)result;  // compile-only contract for the no-callback overload

  EXPECT_FALSE(box.is_empty())
      << "Regression: f.backward(y, box) returned an empty box on a "
         "satisfiable constraint. Either the defaulted-arg signature is "
         "wrong or the HC4 path is broken.";
}

}  // namespace
}  // namespace dreal
