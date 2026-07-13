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
#pragma once

#include <memory>
#include <ostream>
#include <vector>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounding.h"

namespace dreal {

/// Represents the evaluation result of a constraint given a box.
class FormulaEvaluationResult {
 public:
  enum class Type {
    VALID,    ///< Any point in the box satisfies the constraint.
    UNSAT,    ///< There is no point in the box satisfying the constraint.
    UNKNOWN,  ///< It is unknown. It may indicate that there is a
              /// point in the box satisfying the constraint.
  };

  /// Constructs an FormulaEvaluationResult with @p type and @p evaluation.
  FormulaEvaluationResult(Type type, const Box::Interval& evaluation);

  /// Returns the type of this result.
  Type type() const;

  /// Returns the interval evaluation of this result.
  const Box::Interval& evaluation() const;

 private:
  Type type_;

  // Given e₁ rop e₂, it keeps the result of the interval evaluation of (e₁ -
  // e₂) over a box.
  Box::Interval evaluation_;
};

std::ostream& operator<<(std::ostream& os, FormulaEvaluationResult::Type type);
std::ostream& operator<<(std::ostream& os,
                         const FormulaEvaluationResult& result);

// Forward declaration.
class FormulaEvaluatorCell;

/// Class to evaluate a symbolic formula with a box.
class FormulaEvaluator {
 public:
  // No default constructor.
  FormulaEvaluator() = delete;

  /// Default copy constructor.
  FormulaEvaluator(const FormulaEvaluator&) = default;

  /// Default move constructor.
  FormulaEvaluator(FormulaEvaluator&&) = default;

  /// Default copy assign operator.
  FormulaEvaluator& operator=(const FormulaEvaluator&) = default;

  /// Default move assign operator.
  FormulaEvaluator& operator=(FormulaEvaluator&&) = default;

  /// Default destruction
  ~FormulaEvaluator() = default;

  /// Evaluates the constraint/formula with @p box. Interval (gaol) evaluation,
  /// so the caller must hold an UpwardRounding token (FE_UPWARD established).
  FormulaEvaluationResult operator()(const Box& box,
                                     const UpwardRounding& ur) const;

  /// Returns the occurred variables in the formula.
  const Variables& variables() const;

  const Formula& formula() const;

  /// Returns true if the based formula is a simple relational formula which is
  /// in form of `constant relop variable`.
  bool is_simple_relational() const;

  /// Returns true if the based formula is a not-equal formula which is
  /// in form of `e1 != e2` or `!(e1 == e2)`.
  bool is_neq() const;

 private:
  // Constructs an FormulaEvaluator from `ptr`.
  explicit FormulaEvaluator(std::shared_ptr<FormulaEvaluatorCell> ptr);

  std::shared_ptr<FormulaEvaluatorCell> ptr_;

  friend std::ostream& operator<<(std::ostream& os,
                                  const FormulaEvaluator& evaluator);

  friend FormulaEvaluator make_relational_formula_evaluator(const Formula& f);

  friend FormulaEvaluator make_ode_formula_evaluator(const Formula& f,
                                                     bool refine_witness);

  friend FormulaEvaluator make_forall_formula_evaluator(const Formula& f,
                                                        double epsilon,
                                                        double delta,
                                                        int number_of_jobs);
};

/// Creates FormulaEvaluator for a relational formula @p f using @p variables.
FormulaEvaluator make_relational_formula_evaluator(const Formula& f);

/// Creates FormulaEvaluator for an ODE formula @p f. With @p refine_witness
/// (--ode-refine-witness), a positive ODE atom reports its widest variable so
/// ICP refines every ODE dimension below delta before accepting delta-sat;
/// without it, ODE atoms are reported satisfied as-is (fast accept at tube
/// granularity — un-pinned dims keep unrefined-hull midpoint witnesses).
FormulaEvaluator make_ode_formula_evaluator(const Formula& f,
                                            bool refine_witness);

/// Creates FormulaEvaluator for a universally quantified formula @p f
/// using @p variables, @p epsilon, @p delta, and @p number_of_jobs.
FormulaEvaluator make_forall_formula_evaluator(const Formula& f, double epsilon,
                                               double delta,
                                               int number_of_jobs);

std::ostream& operator<<(std::ostream& os, const FormulaEvaluator& evaluator);

}  // namespace dreal

template <> struct fmt::formatter<dreal::FormulaEvaluator> : fmt::ostream_formatter {};
template <> struct fmt::formatter<dreal::FormulaEvaluationResult> : fmt::ostream_formatter {};
template <> struct fmt::formatter<dreal::FormulaEvaluationResult::Type> : fmt::ostream_formatter {};
