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
#include "dreal/solver/filter_assertion.h"

#include <cmath>
#include <limits>

#include "dreal/util/box.h"
#include "dreal/util/exception.h"

namespace dreal {
namespace {

using std::nextafter;
using std::numeric_limits;

// Constrains the @p box with ` box[var].lb() == v`.
FilterAssertionResult UpdateBoundsViaEquality(const Variable& var,
                                              const double v, Box* const box) {
  Box::Interval& intv{(*box)[var]};
  const double lb{intv.lb()};
  const double ub{intv.ub()};
  if (lb == ub && lb == v) {
    return {.filtered = true, .changed = false};
  }
  if (intv.contains(v)) {
    intv = v;
  } else {
    box->set_empty();
  }
  return {.filtered = true, .changed = true};
}

// Constrains the @p box with `box[var] == [lb, ub]`.
FilterAssertionResult UpdateBoundsViaEquality(const Variable& var,
                                              const double new_lb,
                                              const double new_ub,
                                              Box* const box) {
  Box::Interval& intv{(*box)[var]};
  const double old_lb{intv.lb()};
  const double old_ub{intv.ub()};
  if (old_lb == new_lb && old_ub == new_ub) {
    return {.filtered = true, .changed = false};
  }
  intv &= Box::Interval(new_lb, new_ub);
  if (intv.is_empty()) {
    box->set_empty();
  }
  return {.filtered = true, .changed = true};
}

// Constrains the @p box with ` box[var].lb() >= v`.
FilterAssertionResult UpdateLowerBound(const Variable& var, const double new_lb,
                                       Box* const box) {
  Box::Interval& intv{(*box)[var]};
  const double lb{intv.lb()};
  const double ub{intv.ub()};
  if (new_lb <= lb) {
    return {.filtered = true, .changed = false};
  }
  if (new_lb <= ub) {
    intv = Box::Interval(new_lb, ub);
  } else {
    box->set_empty();
  }
  return {.filtered = true, .changed = true};
}

// Constrains the @p box with `box[var].lb() > v`. It changes the
// strict inequality into a non-strict one:
//
//   If var is of CONTINUOUS type: `box[var].lb() >= v + ε`
//                                 where `v + ε` is the smallest representable
//                                 floating-point number bigger than `v`.
//   Otherwise (INTEGER/BINARY)  : Handle it as `box[var].lb() >= v` but keep
//                                 the constraint.
FilterAssertionResult UpdateStrictLowerBound(const Variable& var,
                                             const double v, Box* const box) {
  switch (var.get_type()) {
    case Variable::Type::CONTINUOUS:
      return UpdateLowerBound(var, nextafter(v, numeric_limits<double>::max()),
                              box);
    case Variable::Type::INTEGER:
  case Variable::Type::BINARY:
    return {.filtered = false, .changed = UpdateLowerBound(var, v, box).changed};
    case Variable::Type::BOOLEAN:
      DREAL_UNREACHABLE();
  }
  DREAL_UNREACHABLE();
}

// Constrains the @p box with ` box[var].ub() <= v`.
FilterAssertionResult UpdateUpperBound(const Variable& var, const double new_ub,
                                       Box* const box) {
  Box::Interval& intv{(*box)[var]};
  const double lb{intv.lb()};
  const double ub{intv.ub()};
  if (new_ub >= ub) {
    return {.filtered = true, .changed = false};
  }
  if (new_ub >= lb) {
    intv = Box::Interval(lb, new_ub);
  } else {
    box->set_empty();
  }
  return {.filtered = true, .changed = true};
}

// Constrains the @p box with `box[var].ub() < v`. It changes the
// strict inequality into a non-strict one:
//
//   If var is of CONTINUOUS type: `box[var].ub() <= v - ε`
//                                 where `v - ε` is the largest representable
//                                 floating-point number smaller than `v`.
//   Otherwise (INTEGER/BINARY)  : Handle it as `box[var].ub() <= v` but keep
//                                 the constraint.
FilterAssertionResult UpdateStrictUpperBound(const Variable& var,
                                             const double v, Box* const box) {
  switch (var.get_type()) {
    case Variable::Type::CONTINUOUS:
      return UpdateUpperBound(var, nextafter(v, numeric_limits<double>::lowest()),
                              box);
    case Variable::Type::INTEGER:
    case Variable::Type::BINARY:
      return {.filtered = false, .changed = UpdateUpperBound(var, v, box).changed};
    case Variable::Type::BOOLEAN:
      DREAL_UNREACHABLE();
  }
  DREAL_UNREACHABLE();
}

class AssertionFilter {
 public:
  FilterAssertionResult Process(const Formula& f, Box* const box) const {
    return Visit(f, box, true);
  }

  FilterAssertionResult Visit(const Formula& f, Box* const box,
                              const bool polarity) const {
    return VisitFormula<FilterAssertionResult>(this, f, box, polarity);
  }
  static FilterAssertionResult VisitFalse(const Formula& /* unused */,
                                          Box* const /* unused */,
                                          const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitTrue(const Formula& /* unused */,
                                         Box* const /* unused */,
                                         const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitVariable(const Formula& /* unused */,
                                             Box* const /* unused */,
                                             const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitEqualTo(const Formula& f, Box* const box,
                                            const bool polarity) {
    if (!polarity) {
      return {.filtered = false, .changed = false};
    }
    const Expression& lhs{get_lhs_expression(f)};
    const Expression& rhs{get_rhs_expression(f)};
    if (is_variable(lhs)) {
      if (is_constant(rhs)) {
        // var = v
        const Variable& var{get_variable(lhs)};
        const double v{get_constant_value(rhs)};
        return UpdateBoundsViaEquality(var, v, box);
      }
      if (is_real_constant(rhs)) {
        // var = [lb, ub]
        const Variable& var{get_variable(lhs)};
        const double lb{get_lb_of_real_constant(rhs)};
        const double ub{get_ub_of_real_constant(rhs)};
        return UpdateBoundsViaEquality(var, lb, ub, box);
      }
    }
    if (is_variable(rhs)) {
      if (is_constant(lhs)) {
        // v = var
        const double v{get_constant_value(lhs)};
        const Variable& var{get_variable(rhs)};
        return UpdateBoundsViaEquality(var, v, box);
      }
      if (is_real_constant(lhs)) {
        // [lb, ub] = var
        const Variable& var{get_variable(rhs)};
        const double lb{get_lb_of_real_constant(lhs)};
        const double ub{get_ub_of_real_constant(lhs)};
        return UpdateBoundsViaEquality(var, lb, ub, box);
      }
    }
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitNotEqualTo(const Formula& f, Box* const box,
                                               const bool polarity) {
    // Because (x != y) is !(x == y).
    return VisitEqualTo(f, box, !polarity);
  }
  static FilterAssertionResult VisitGreaterThan(const Formula& f,
                                                Box* const box,
                                                const bool polarity) {
    const Expression& lhs{get_lhs_expression(f)};
    const Expression& rhs{get_rhs_expression(f)};
    if (is_variable(lhs)) {
      if (is_constant(rhs)) {
        const Variable& var{get_variable(lhs)};
        const double v{get_constant_value(rhs)};
        if (polarity) {
          // var > v
          return UpdateStrictLowerBound(var, v, box);
        } else {
          // !(var > v) => (var <= v)
          return UpdateUpperBound(var, v, box);
        }
      }
      if (is_real_constant(rhs)) {
        const Variable& var{get_variable(lhs)};
        if (polarity) {
          // var > [lb, ub] => var > lb
          return UpdateStrictLowerBound(var, get_lb_of_real_constant(rhs), box);
        } else {
          // !(var > [lb, ub]) => (var <= [lb, ub]) => (var <= ub)
          return UpdateUpperBound(var, get_ub_of_real_constant(rhs), box);
        }
      }
    }
    if (is_variable(rhs)) {
      if (is_constant(lhs)) {
        const double v{get_constant_value(lhs)};
        const Variable& var{get_variable(rhs)};
        if (polarity) {
          // v > var
          return UpdateStrictUpperBound(var, v, box);
        } else {
          // !(v > var) => (v <= var)
          return UpdateLowerBound(var, v, box);
        }
      }
      if (is_real_constant(lhs)) {
        const Variable& var{get_variable(rhs)};
        if (polarity) {
          // [lb, ub] > var => ub > var
          return UpdateStrictUpperBound(var, get_ub_of_real_constant(lhs), box);
        } else {
          // !([lb, ub] > var) => ([lb, ub] <= var) => (lb <= var)
          return UpdateLowerBound(var, get_lb_of_real_constant(lhs), box);
        }
      }
    }
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitGreaterThanOrEqualTo(const Formula& f,
                                                         Box* const box,
                                                         const bool polarity) {
    const Expression& lhs{get_lhs_expression(f)};
    const Expression& rhs{get_rhs_expression(f)};
    if (is_variable(lhs)) {
      if (is_constant(rhs)) {
        const Variable& var{get_variable(lhs)};
        const double v{get_constant_value(rhs)};
        if (polarity) {
          // var >= v
          return UpdateLowerBound(var, v, box);
        } else {
          // !(var >= v) => (var < v)
          return UpdateStrictUpperBound(var, v, box);
        }
      }
      if (is_real_constant(rhs)) {
        const Variable& var{get_variable(lhs)};
        if (polarity) {
          // var >= [lb, ub] => var >= lb
          return UpdateLowerBound(var, get_lb_of_real_constant(rhs), box);
        } else {
          // !(var >= v) => (var < [lb, ub]) => (var < ub)
          return UpdateStrictUpperBound(var, get_ub_of_real_constant(rhs), box);
        }
      }
    }
    if (is_variable(rhs)) {
      if (is_constant(lhs)) {
        const double v{get_constant_value(lhs)};
        const Variable& var{get_variable(rhs)};
        if (polarity) {
          // v >= var
          return UpdateUpperBound(var, v, box);
        } else {
          // !(v >= var) => (v < var)
          return UpdateStrictLowerBound(var, v, box);
        }
      }
      if (is_real_constant(lhs)) {
        const Variable& var{get_variable(rhs)};
        if (polarity) {
          // [lb, ub] >= var => ub >= var
          return UpdateUpperBound(var, get_ub_of_real_constant(lhs), box);
        } else {
          // !([lb, ub] >= var) => ([lb, ub] < var) => (lb < var)
          return UpdateStrictLowerBound(var, get_lb_of_real_constant(lhs), box);
        }
      }
    }
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitLessThan(const Formula& f, Box* const box,
                                             const bool polarity) {
    // Because x < y is !(x >= y).
    return VisitGreaterThanOrEqualTo(f, box, !polarity);
  }
  static FilterAssertionResult VisitLessThanOrEqualTo(const Formula& f,
                                                      Box* const box,
                                                      const bool polarity) {
    // Because x <= y is !(x > y).
    return VisitGreaterThan(f, box, !polarity);
  }
  static FilterAssertionResult VisitConjunction(const Formula& /* unused */,
                                                Box* const /* unused */,
                                                const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitDisjunction(const Formula& /* unused */,
                                                Box* const /* unused */,
                                                const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }
  FilterAssertionResult VisitNegation(const Formula& f, Box* const box,
                                      const bool polarity) const {
    return Visit(get_operand(f), box, !polarity);
  }
  static FilterAssertionResult VisitForall(const Formula& /* unused */,
                                           Box* const /* unused */,
                                           const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitForallT(const Formula& /* unused */,
                                         Box* const /* unused */,
                                         const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }
  static FilterAssertionResult VisitIntegral(const Formula& /* unused */,
                                         Box* const /* unused */,
                                         const bool /* unused */) {
    return {.filtered = false, .changed = false};
  }

  // Makes VisitFormula a friend of this class so that it can use private
  // operator()s.
  friend FilterAssertionResult
  drake::symbolic::VisitFormula<FilterAssertionResult>(AssertionFilter*,
                                                       const Formula&,
                                                       dreal::Box* const&,
                                                       const bool&);
};
}  // namespace

FilterAssertionResult FilterAssertion(const Formula& assertion,
                                      Box* const box) {
  return AssertionFilter{}.Process(assertion, box);
}
}  // namespace dreal
