// Regression test for ncsys-lab/ibex-lib@dreal-perf-patches commit d2b978b9
// ("HC4Revise: report partial narrowings on EmptyBoxException").
//
// `HC4Revise::proj`'s catch block was:
//
//     } catch(EmptyBoxException&) {
//         x.set_empty();
//         return false;
//     }
//
// Any leaf whose `d.args` entry was narrowed during the partial backward
// sweep before the contradiction got no callback event. Callers using
// the callback to populate a per-variable change set had to over-
// approximate to "every variable might have caused this".
//
// The fix invokes `d.read_arg_domains(x, callback)` in the catch block
// before `set_empty()`. The callback fires for each component whose
// `d.args` value differs from its box value; `set_empty()` then drives
// the final infeasibility signal.
//
// ibex pitfall (relevant to both tests below): `ibex::Domain`'s scalar
// ctor takes the Interval by reference and stores its ADDRESS
// (`is_reference=true`). Passing a temporary `Interval(...)` directly
// to `Domain(...)` leaves Domain holding a dangling pointer once the
// temporary dies at the end of the full-expression — subsequent reads
// inside `f.backward` see garbage. Always bind the Interval to a named
// lvalue first.

#include <gtest/gtest.h>

#include <cfenv>
#include <vector>

#include <ibex.h>

namespace dreal {
namespace {

struct Notification {
  int index;
  ibex::Interval old_value;
  ibex::Interval new_value;
};

// gtest starts in FE_TONEAREST; gaol's IA needs FE_UPWARD for sound
// directed rounding. Reset at the top of each TEST body to match
// gaol's runtime expectation (companion ibex_log_pow_edge_cases tests
// use the same pattern).

// Scenario A: the "happy path" — backward succeeds, callback fires for
// the narrowed leaves. Baseline that patch d2b978b9 must not regress.
TEST(IbexBackwardCallbackPartialEmpty, HappyPathStillNotifiesPostPatch) {
  std::fesetround(FE_UPWARD);
  // f(x, y) = x + y, x,y in [0, 1], constraint y = 1.5.
  const auto& xs = ibex::ExprSymbol::new_();
  const auto& ys = ibex::ExprSymbol::new_();
  ibex::Function f(xs, ys, xs + ys);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 1.0);
  box[1] = ibex::Interval(0.0, 1.0);

  std::vector<Notification> notes;
  auto cb = [&notes](int idx, const ibex::Interval& old_val,
                     const ibex::Interval& new_val) {
    notes.push_back({idx, old_val, new_val});
  };

  // Domain holds a pointer to the Interval — bind to a named lvalue.
  ibex::Interval target_iv{1.5, 1.5};
  ibex::Domain target(target_iv);
  f.backward(target, box, cb);

  EXPECT_FALSE(box.is_empty())
      << "Sanity: x+y=1.5 with x,y in [0,1] is satisfiable.";
  EXPECT_FALSE(notes.empty())
      << "Callback should fire for each narrowed leaf.";
  for (const auto& n : notes) {
    EXPECT_NE(n.old_value, n.new_value)
        << "Callback fired with old == new — should not happen.";
  }
}

// Scenario B: empty box on an infeasible constraint runs the catch path.
//
// f(x, y) = x + y with x,y in [0, 1], constraint = -5. Forward image
// [0, 2] doesn't contain -5, so the root narrow empties the constraint
// domain and HC4Revise throws. The patch's added read_arg_domains call
// fires no callbacks here (the root throw happens before any leaf was
// touched, so `d.args[*j]` still equals `box[*j]`), but the catch block
// must still set the box empty without crashing — verifying the catch
// path is structurally sound.
//
// The per-variable invariant "callback fires => old != new" is also
// checked: even when the patch fires no events on root throws, it must
// not fire spurious ones either.
TEST(IbexBackwardCallbackPartialEmpty, InfeasibleConstraintEmptiesBox) {
  std::fesetround(FE_UPWARD);
  const auto& xs = ibex::ExprSymbol::new_();
  const auto& ys = ibex::ExprSymbol::new_();
  ibex::Function f(xs, ys, xs + ys);
  ibex::IntervalVector box(2);
  box[0] = ibex::Interval(0.0, 1.0);
  box[1] = ibex::Interval(0.0, 1.0);

  std::vector<Notification> notes;
  auto cb = [&notes](int idx, const ibex::Interval& old_val,
                     const ibex::Interval& new_val) {
    notes.push_back({idx, old_val, new_val});
  };

  // -5 is far outside the forward image [0, 2] of x+y. Backward must
  // empty the box via the catch path.
  ibex::Interval target_iv{-5.0, -5.0};
  ibex::Domain target(target_iv);
  f.backward(target, box, cb);

  EXPECT_TRUE(box.is_empty())
      << "Infeasible constraint should empty the box via the catch path.";
  // No spurious callbacks: the patch's read_arg_domains in the catch
  // block must only fire when args differ from box.
  for (const auto& n : notes) {
    EXPECT_NE(n.old_value, n.new_value)
        << "Patch must not fire callbacks with old == new (var " << n.index
        << ").";
  }
}

}  // namespace
}  // namespace dreal
