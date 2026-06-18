// Regression test for ncsys-lab/ibex-lib@dreal-perf-patches commit 1836b569
// ("function: copy old-value in backward callback to avoid alias").
//
// In `src/function/ibex_ExprDomain.h` ExprTemplateDomain::read_arg_domains'
// scalar branch was:
//
//     for (auto j : used_vars) {
//         const auto& old_value = box[*j];          // pre-fix: reference
//         const auto& new_value = args[*j].i();
//         if (old_value != new_value)
//             callback(*j, old_value, new_value);
//         box[*j] = new_value;                      // overwrites box[*j]
//     }
//
// `old_value` was bound as a const reference to `box[*j]` — i.e. it aliased
// the box element that the next line overwrites. The callback parameter
// (`const Interval& old_value`) was therefore bound, through the local, to
// the same storage as `box[*j]`. Any callback that retained `&old_value`
// for use after read_arg_domains finished would observe the NEW value, not
// the old one.
//
// The fix replaces the const-reference binding with a by-value copy:
//
//     const Interval old_value = box[*j];           // post-fix: local copy
//
// After the fix, `&old_value` points to a function-local Interval, not into
// the box's storage.
//
// Detecting the bug from outside without invoking UB is subtle. The
// callback parameter's lifetime ends at callback return, so any dereference
// of a retained `&old_value` post-callback is undefined behavior in both
// pre-fix and post-fix code paths. But the *address values themselves* are
// well-defined integers — converting them to `std::uintptr_t` during the
// callback (when the binding is valid) and comparing them afterward is a
// purely arithmetic operation. That comparison distinguishes the two
// regimes:
//
//   - Pre-fix:  &old_value == &box[idx]  (true alias).
//   - Post-fix: &old_value != &box[idx]  (separate stack-local).
//
// This file asserts the post-fix property.

#include <gtest/gtest.h>

#include <cstdint>

#include <ibex.h>

namespace dreal {
namespace {

TEST(IbexBackwardCallbackAlias, OldValueParamIsNotAliasOfBoxComponent) {
  // x + y = 0.5 with x in [0, 0.3], y in [0, 1] — the same setup as
  // ibex_backward_callback_test.cc's `CallbackFiresForNarrowedVariable`.
  // The narrowing on y guarantees at least one callback invocation.
  const auto& xs = ibex::ExprSymbol::new_();
  const auto& ys = ibex::ExprSymbol::new_();
  ibex::Function f(xs, ys, xs + ys);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 0.3);
  box[1] = ibex::Interval(0.0, 1.0);

  int first_idx = -1;
  std::uintptr_t first_old_addr = 0;
  auto cb = [&](int idx, const ibex::Interval& old_val,
                const ibex::Interval& /*new_val*/) {
    if (first_idx == -1) {
      first_idx = idx;
      // Convert the parameter's address to an integer while the binding is
      // still valid. The integer survives the callback return without UB.
      first_old_addr = reinterpret_cast<std::uintptr_t>(&old_val);
    }
  };

  // ibex::Domain holds the Interval by reference (`is_reference=true`),
  // so passing a temporary makes Domain dangle. Bind to a named lvalue.
  ibex::Interval target_iv{0.5, 0.5};
  ibex::Domain y_dom(target_iv);
  f.backward(y_dom, box, cb);

  ASSERT_NE(first_idx, -1)
      << "Callback never fired; cannot evaluate alias property.";

  const std::uintptr_t box_addr =
      reinterpret_cast<std::uintptr_t>(&box[first_idx]);

  EXPECT_NE(first_old_addr, box_addr)
      << "Regression: the `old_value` callback parameter aliases box["
      << first_idx
      << "]. Patch 1836b569 missing? Pre-fix the const-reference binding "
         "makes &old_value identical to &box[idx]; any caller that retains "
         "the reference past the callback observes the new (post-write) "
         "value instead of the old one.";
}

}  // namespace
}  // namespace dreal
