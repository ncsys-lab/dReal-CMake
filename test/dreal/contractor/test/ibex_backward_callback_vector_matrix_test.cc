// Regression test for the audit fix "function: fire backward callback for
// non-scalar args" (commit f04f5db5 on ncsys-lab/ibex-lib@dreal-perf-patches).
//
// Pre-fix, `ExprTemplateDomain::read_arg_domains`'s non-scalar branch in
// `src/function/ibex_ExprDomain.h` delegated component-write back into the
// box to the callback-less `load(box, args, used)` overload from
// `src/arithmetic/ibex_TemplateDomain.h`. Any `Function` constructed with a
// vector- or matrix-typed `ExprSymbol` would therefore silently miss every
// per-component narrowing notification, breaking the contract that "if
// `box[i]` changes during `Function::backward`, the callback fires for `i`".
//
// dReal's `ibex_converter` only creates scalar symbols today (verified by
// grep, no `Dim::col_vec` / `Dim::row_vec` / `Dim::matrix` callers), so this
// is not a soundness regression for dReal in the present configuration. But
// the patch series is intended for upstream submission, and other ibex
// consumers (Codac, custom users) do create vector/matrix symbols. The
// callback must be sound for them too.
//
// This test constructs a single-vector-arg Function with a non-trivial
// backward narrowing on both vector components and asserts that the
// callback fires for each component's box index. Without the fix, the
// callback fires for none of them.

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include <ibex.h>

namespace dreal {
namespace {

struct Notification {
  int index;
  ibex::Interval old_value;
  ibex::Interval new_value;
};

TEST(IbexBackwardCallbackVectorMatrix, CallbackFiresForEachVectorComponent) {
  // f(x, y) = x[0] + x[1] + y, where x is a 2-vector and y is scalar.
  // Backward from a tight scalar constraint must narrow at least one of
  // x[0], x[1] when their input ranges are wider than the constraint allows.
  const auto& xs = ibex::ExprSymbol::new_("x", ibex::Dim::col_vec(2));
  const auto& ys = ibex::ExprSymbol::new_("y");
  ibex::Function f(xs, ys, xs[0] + xs[1] + ys);

  // Box: 3 components — x[0], x[1], y. Initial ranges chosen so that the
  // constraint sum = 5 narrows all three meaningfully.
  ibex::IntervalVector box(3);
  box[0] = ibex::Interval(0.0, 10.0);  // x[0]
  box[1] = ibex::Interval(0.0, 10.0);  // x[1]
  box[2] = ibex::Interval(0.0, 10.0);  // y

  std::vector<Notification> notes;
  auto cb = [&notes](int idx, const ibex::Interval& old_val,
                     const ibex::Interval& new_val) {
    notes.push_back({idx, old_val, new_val});
  };

  // Constraint: f = 5 exactly. With each variable in [0, 10], backward must
  // narrow each one to [0, 5] (max possible to leave room for sum = 5 with
  // the others at their minimum 0).
  ibex::Domain y_dom(ibex::Interval(5.0, 5.0));
  f.backward(y_dom, box, cb);

  // Collect indices that received notifications.
  std::set<int> notified;
  for (const auto& n : notes) notified.insert(n.index);

  // Pre-fix expectation: notified is EMPTY (the non-scalar load() drop-on-
  // the-floor bug). Post-fix expectation: notified contains all 3 box
  // indices, because each variable's range tightened from [0,10] to [0,5].
  EXPECT_TRUE(notified.count(0) > 0)
      << "callback should fire for x[0] (box index 0); pre-fix it would be "
         "silently dropped via the non-scalar load() path";
  EXPECT_TRUE(notified.count(1) > 0)
      << "callback should fire for x[1] (box index 1); same bug";
  EXPECT_TRUE(notified.count(2) > 0)
      << "callback should fire for y (box index 2)";

  // Also assert box was actually narrowed (sanity: if backward did nothing,
  // the test would trivially pass for the wrong reason).
  EXPECT_LE(box[0].ub(), 5.0)
      << "x[0] should have been narrowed to <= 5 by the sum=5 constraint";
  EXPECT_LE(box[1].ub(), 5.0)
      << "x[1] should have been narrowed similarly";
  EXPECT_LE(box[2].ub(), 5.0)
      << "y should have been narrowed similarly";
}

}  // namespace
}  // namespace dreal
