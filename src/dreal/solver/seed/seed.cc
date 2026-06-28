/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0
*/
#include "dreal/solver/seed/seed.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <random>
#include <utility>

#include "dreal/optimization/nlopt_optimizer.h"
#include "dreal/util/logging.h"
#include "dreal/util/nnfizer.h"
#include "dreal/util/rounded_interval.h"

namespace dreal {

using std::vector;

namespace {

// True iff dimension i of `box` has finite bounds. Only finite dims can be
// sampled / pinned; the rest (e.g. equality-defined CSE auxiliary variables that
// carry no explicit bound) are LEFT at full range in the seed box and resolved
// by the contractor's HC4 propagation of the linking equalities. This is the key
// to seeding the odeexpr Lyapunov instances, whose boxes mix bounded primary
// variables with unbounded derived ones.
bool FiniteDim(const Box& box, const int i) {
  return std::isfinite(box[i].lb()) && std::isfinite(box[i].ub());
}

// True iff variable v has finite bounds in box.
bool FiniteVar(const Box& box, const Variable& v) {
  const Box::Interval& iv{box[v]};
  return std::isfinite(iv.lb()) && std::isfinite(iv.ub());
}

}  // namespace

// Indices of the finite (samplable) dimensions of `box`.
vector<int> FiniteDims(const Box& box) {
  vector<int> dims;
  for (int i = 0; i < box.size(); ++i) {
    if (FiniteDim(box, i)) {
      dims.push_back(i);
    }
  }
  return dims;
}

// Substitution mapping each unbounded (no explicit bound) variable that an
// equality `v == e` defines to its defining expression `e`, with chained CSEs
// resolved to a fixpoint so every RHS references only bounded variables. This is
// what lets COBYLA see the hard constraint as a self-contained function of the
// bounded primaries: substituting these out removes the equality constraints
// (which COBYLA handles poorly) and the unbounded CSE dims (whose 0-init pulls
// the optimizer toward the infeasible origin) from nlopt's view entirely.
ExpressionSubstitution DerivedSubstitution(
    const vector<FormulaEvaluator>& formula_evaluators, const Box& box) {
  ExpressionSubstitution subst;
  for (const FormulaEvaluator& fe : formula_evaluators) {
    const Formula& f{fe.formula()};
    if (!is_equal_to(f)) {
      continue;
    }
    const Expression& lhs{get_lhs_expression(f)};
    const Expression& rhs{get_rhs_expression(f)};
    if (is_variable(lhs) && !FiniteVar(box, get_variable(lhs))) {
      subst.emplace(get_variable(lhs), rhs);
    } else if (is_variable(rhs) && !FiniteVar(box, get_variable(rhs))) {
      subst.emplace(get_variable(rhs), lhs);
    }
  }
  // Resolve chains (a CSE defined in terms of another): substitute the map into
  // its own RHSs until a fixpoint. Bounded by subst.size() passes (acyclic defs).
  for (size_t pass = 0; pass < subst.size(); ++pass) {
    bool changed{false};
    for (auto& [v, e] : subst) {
      const Expression resolved{e.Substitute(subst)};
      if (!resolved.EqualTo(e)) {
        e = resolved;
        changed = true;
      }
    }
    if (!changed) {
      break;
    }
  }
  return subst;
}

// N Latin-hypercube sample points over the FINITE subspace of `box` (each a
// full-width vector indexed by box dimension order; unbounded entries are left
// at 0 and ignored downstream). Pure geometry — no constraint machinery and no
// gradient, hence immune to the flat-center (∇=0) stall that threatens local
// optimization. One stratified permutation per finite dimension, jittered within
// each stratum.
vector<vector<double>> LatinHypercubeSamples(const Box& box,
                                             const vector<int>& dims,
                                             const int n, const uint32_t seed) {
  std::mt19937 rng{seed};
  std::uniform_real_distribution<double> jitter{0.0, 1.0};
  vector<vector<int>> strata(dims.size(), vector<int>(n));
  for (auto& s : strata) {
    for (int k = 0; k < n; ++k) {
      s[k] = k;
    }
    std::shuffle(s.begin(), s.end(), rng);
  }
  vector<vector<double>> points(n, vector<double>(box.size(), 0.0));
  for (int k = 0; k < n; ++k) {
    for (size_t j = 0; j < dims.size(); ++j) {
      const int i{dims[j]};
      const double lb{box[i].lb()};
      const double ub{box[i].ub()};
      const double frac{(strata[j][k] + jitter(rng)) / n};
      points[k][i] = lb + frac * (ub - lb);
    }
  }
  return points;
}

// Multi-start COBYLA seeds: run derivative-free local optimization toward
// feasibility from the box center plus (seed_samples - 1) LHS starts, returning
// each optimized point. COBYLA's `center ± rhobeg` simplex probes off-center
// even where ∇=0, so even a single center start can escape the flat center; the
// extra LHS starts add coverage for a tiny or multi-modal feasible region.
//
// A start that throws (nlopt roundoff_limited etc.) contributes no candidate and
// is skipped — this is the cache/recompute carve-out, NOT a silent fallback: the
// canonical complete ICP search (the root box, already on the stack) is the real
// path and runs regardless; a failed speculative start just yields no extra box.
vector<vector<double>> NloptSeeds(
    const vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    const Config& config, const UpwardRounding& ur) {
  // Eliminate equality-defined CSE auxiliaries so COBYLA optimizes the hard
  // constraint as a self-contained function of the bounded primary variables.
  const ExpressionSubstitution derived{
      DerivedSubstitution(formula_evaluators, box)};
  // Optimize over a SUB-BOX of only the finite (bounded) variables. The
  // unbounded CSE dims were substituted out of every constraint, so they belong
  // in nothing — keeping them in nlopt's box (with ±inf bounds, init 0) lets
  // COBYLA wander them to inf and trip the evaluator's nan check on every call.
  // Excluding them entirely both fixes that and focuses the search.
  const vector<int> dims{FiniteDims(box)};
  Box sub;
  for (const int i : dims) {
    sub.Add(box.variable(i), box[i].lb(), box[i].ub());
  }
  NloptOptimizer opt{nlopt::algorithm::LN_COBYLA, sub, config};
  const Nnfizer nnfizer{};
  Expression objective{};
  for (const FormulaEvaluator& fe : formula_evaluators) {
    const Formula& f{fe.formula()};
    if (fe.is_neq() || is_equal_to(f)) {
      // Disequality carries no feasibility signal and nlopt cannot encode it;
      // equalities are either the CSE definitions just substituted out, or
      // (rare) genuine constraints that COBYLA handles poorly — in both cases
      // the box-verify (EvaluateBox) is the real arbiter, so dropping them from
      // the proposer only affects guidance, never soundness. Skip.
      continue;
    }
    // dReal stores `e1 > e2` as `¬(e1 ≤ e2)`; push the negation into the
    // relational so ConstraintViolation sees a positive `>`/`<` (else the
    // objective is empty and nlopt aborts with "NULL args").
    const Formula f_sub{nnfizer.Convert(f.Substitute(derived), true)};
    objective += ConstraintViolation(f_sub);
    opt.AddConstraint(f_sub);
  }
  if (!is_zero(objective)) {
    opt.SetMinObjective(objective);
  }

  // Starts over the sub-box: the center plus (seed_samples - 1) LHS points.
  const vector<int> sub_dims{FiniteDims(sub)};  // all of them
  const int starts{std::max(1, config.seed_samples())};
  vector<vector<double>> inits;
  inits.reserve(starts);
  vector<double> center(sub.size());
  for (int j = 0; j < sub.size(); ++j) {
    center[j] = safe_mid(sub[j], ur);
  }
  inits.push_back(std::move(center));
  if (starts > 1) {
    for (vector<double>& p :
         LatinHypercubeSamples(sub, sub_dims, starts - 1, config.random_seed())) {
      inits.push_back(std::move(p));
    }
  }

  vector<vector<double>> points;
  points.reserve(inits.size());
  double best_optf{std::numeric_limits<double>::infinity()};
  for (vector<double>& init : inits) {
    double opt_f{0.0};
    try {
      opt.Optimize(&init, &opt_f);  // updates init in place; rounding-guarded
      best_optf = std::min(best_optf, opt_f);
      // Map the sub-box solution back to a full-width point: finite dims filled,
      // unbounded (CSE) dims left 0 — SmallBoxAround pins only the finite dims
      // and the contractor re-derives the rest from the linking equalities.
      vector<double> full(box.size(), 0.0);
      for (size_t j = 0; j < dims.size(); ++j) {
        full[dims[j]] = init[j];
      }
      points.push_back(std::move(full));
    } catch (const std::exception& e) {
      DREAL_LOG_DEBUG("SeedBoxes: nlopt start failed: {}", e.what());
    }
  }
  DREAL_LOG_INFO("[seed-nlopt] starts={} derived={} best_optf={}", inits.size(),
                 derived.size(), best_optf);
  return points;
}

// A small sound box around proposal point `pt`, pinning only the FINITE
// dimensions (indexed by box order); unbounded (equality-defined) dims are left
// at full range for the contractor's HC4 to narrow from the linking equalities.
// Each pinned endpoint is rounded outward (sub_down/add_up) so the interval
// soundly contains [pt_i - w, pt_i + w]; the intersection with `box` (exact)
// keeps the dimension inside the declared domain, so a verified delta-box is a
// genuine in-domain model. `pt` is only a proposal — soundness rides entirely on
// this construction plus the downstream EvaluateBox.
Box SmallBoxAround(const Box& box, const vector<double>& pt,
                   const double half_width, const UpwardRounding& ur) {
  Box b{box};
  const Exact w{half_width};
  for (int i = 0; i < b.size(); ++i) {
    if (!FiniteDim(box, i)) {
      continue;
    }
    const Box::Interval around{make_sound_interval(
        sub_down(Exact{pt[i]}, w, ur), add_up(Exact{pt[i]}, w, ur))};
    b[i] = around & box[i];
  }
  return b;
}

bool AllRelational(const vector<FormulaEvaluator>& formula_evaluators) {
  for (const FormulaEvaluator& fe : formula_evaluators) {
    const Formula& f{fe.formula()};
    if (is_forall(f) || f.include_ode()) {
      return false;
    }
  }
  return true;
}

vector<Box> SeedBoxes(const vector<FormulaEvaluator>& formula_evaluators,
                      const Box& box, const Config& config,
                      const UpwardRounding& ur) {
  const vector<int> dims{FiniteDims(box)};
  if (dims.empty()) {
    // Nothing to pin — seeding cannot shrink the box. The complete search runs.
    return {};
  }
  const vector<vector<double>> points{
      NloptSeeds(formula_evaluators, box, config, ur)};
  vector<Box> boxes;
  boxes.reserve(points.size());
  const double half_width{config.precision()};
  for (const vector<double>& pt : points) {
    boxes.push_back(SmallBoxAround(box, pt, half_width, ur));
  }
  return boxes;
}

}  // namespace dreal
