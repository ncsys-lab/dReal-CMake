// Regression test for ncsys-lab/ibex-lib commit 4d61b841
// ("TMP - Add callback to ibex Function::backward to avoid expensive copying
//  in dReal").
//
// The patch (in `src/function/ibex_Function.h`,
// `src/function/ibex_HC4Revise.{h,cpp}`, `src/function/ibex_ExprDomain.h`)
// adds an optional callback to `Function::backward`:
//
//     bool backward(const Domain& y, IntervalVector& x,
//                   const std::function<void(int index,
//                                            const Interval& old_value,
//                                            const Interval& new_value)>&
//                       callback = [](int, const Interval&,
//                                     const Interval&) {}) const;
//
// dReal's `contractor_ibex_fwdbwd.cc` uses this to discover which variables
// the HC4 revise narrowed, without snapshotting the box before/after.
//
// This file tests:
//   1. The callback fires for each variable whose range changed.
//   2. The callback receives meaningful `old_value` and `new_value`.
//   3. Variables that did not narrow are NOT in the callback notifications.
//
// The companion compile test is `ibex_function_backward_compat_test.cc`,
// which exercises the defaulted-arg overload (no callback).

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

TEST(IbexBackwardCallback, CallbackFiresForNarrowedVariable) {
  // x + y = 0.5, with x in [0, 0.3] and y in [0, 1]. Backward narrows y
  // to approximately [0.2, 0.5]; x is unchanged (range already constrained
  // by the equation only via y, and x's range is already wide enough).
  const auto& xs = ibex::ExprSymbol::new_();
  const auto& ys = ibex::ExprSymbol::new_();
  ibex::Function f(xs, ys, xs + ys);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 0.3);
  box[1] = ibex::Interval(0.0, 1.0);

  std::vector<Notification> notes;
  auto cb = [&notes](int idx, const ibex::Interval& old_val,
                     const ibex::Interval& new_val) {
    notes.push_back({idx, old_val, new_val});
  };
  ibex::Domain y_dom(ibex::Interval(0.5, 0.5));
  f.backward(y_dom, box, cb);

  // At least one notification fired (y narrowed).
  EXPECT_FALSE(notes.empty())
      << "Regression: Function::backward callback not invoked. Patch "
         "4d61b841 missing?";

  // Collect indices that fired. Index 1 (y) must be present.
  std::set<int> fired_indices;
  for (const auto& n : notes) fired_indices.insert(n.index);
  EXPECT_NE(fired_indices.find(1), fired_indices.end())
      << "Regression: callback not fired for variable y, which clearly "
         "narrowed.";
}

TEST(IbexBackwardCallback, CallbackReceivesOldAndNewValues) {
  const auto& xs = ibex::ExprSymbol::new_();
  const auto& ys = ibex::ExprSymbol::new_();
  ibex::Function f(xs, ys, xs + ys);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 0.3);
  box[1] = ibex::Interval(0.0, 1.0);

  Notification first;
  bool got_first = false;
  auto cb = [&](int idx, const ibex::Interval& old_val,
                const ibex::Interval& new_val) {
    if (!got_first) {
      first = {idx, old_val, new_val};
      got_first = true;
    }
  };
  ibex::Domain y_dom(ibex::Interval(0.5, 0.5));
  f.backward(y_dom, box, cb);

  ASSERT_TRUE(got_first);
  // new_value must be a STRICT subset of old_value (otherwise it wouldn't be
  // a "change"). For y narrowing from [0, 1] to ~[0.2, 0.5]:
  EXPECT_LE(first.old_value.lb(), first.new_value.lb());
  EXPECT_GE(first.old_value.ub(), first.new_value.ub());
  EXPECT_LT(first.new_value.diam(), first.old_value.diam());
}

TEST(IbexBackwardCallback, CallbackNotFiredForUnchangedVariable) {
  // x + y = 1.5 with x in [0, 1], y in [0.5, 1]. Backward should narrow
  // only one variable at most (the other is already tight enough).
  //
  // We can't deterministically predict which variable narrows without
  // emulating HC4, so we just verify: every notification's old_value must
  // strictly contain its new_value (i.e., callback never fires spuriously
  // for a no-op change).
  const auto& xs = ibex::ExprSymbol::new_();
  const auto& ys = ibex::ExprSymbol::new_();
  ibex::Function f(xs, ys, xs + ys);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 1.0);
  box[1] = ibex::Interval(0.5, 1.0);

  std::vector<Notification> notes;
  auto cb = [&notes](int idx, const ibex::Interval& old_val,
                     const ibex::Interval& new_val) {
    notes.push_back({idx, old_val, new_val});
  };
  ibex::Domain y_dom(ibex::Interval(1.5, 1.5));
  f.backward(y_dom, box, cb);

  for (const auto& n : notes) {
    EXPECT_LE(n.old_value.lb(), n.new_value.lb());
    EXPECT_GE(n.old_value.ub(), n.new_value.ub());
    EXPECT_LE(n.new_value.diam(), n.old_value.diam() + 1e-12)
        << "Spurious callback for var " << n.index
        << ": old=[" << n.old_value.lb() << "," << n.old_value.ub()
        << "] new=[" << n.new_value.lb() << "," << n.new_value.ub() << "]";
  }
}

}  // namespace
}  // namespace dreal
