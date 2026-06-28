/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#include <cstdint>
#include <vector>

#include "dreal/solver/config.h"
#include "dreal/solver/formula_evaluator.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/rounding.h"

namespace dreal {

/// True iff every evaluator's formula is a plain relational constraint — no
/// `forall`, no ODE/integral. The seed-and-verify gate: only pure-NRA theory
/// calls are seeded (`forall` already uses nlopt internally; per-node proposal
/// over ODE flows is too costly), and the nlopt proposer can only build a
/// relational objective. Predicates match theory_solver.cc's evaluator dispatch.
bool AllRelational(const std::vector<FormulaEvaluator>& formula_evaluators);

/// Proposes small SOUND candidate boxes (each ⊆ @p box) for the `--seed-samples`
/// seed-and-verify pre-pass (multi-start COBYLA). The caller pushes these
/// onto the ICP stack to be explored first; the existing prune+`EvaluateBox`
/// loop is the SOLE arbiter of delta-SAT, so a poor candidate can never cause a
/// false delta-sat — this is a COMPLETENESS-only speed optimization, not a
/// fallback. Returns empty when seeding is inapplicable (e.g. an unbounded box).
///
/// @pre AllRelational(formula_evaluators); caller holds an UpwardRounding token.
std::vector<Box> SeedBoxes(
    const std::vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    const Config& config, const UpwardRounding& ur);

// ---------------------------------------------------------------------------
// Internal units of the SeedBoxes pipeline, declared here (rather than kept
// file-static) so they can be unit-tested directly and adversarially. They are
// not part of the seeding entry point — callers use AllRelational / SeedBoxes.
// Mechanism documented in docs/seeding.md.
// ---------------------------------------------------------------------------

/// Indices of the finite (both-bounds-finite, hence samplable) dimensions of
/// @p box. Unbounded dims (e.g. equality-defined CSE auxiliaries) are excluded;
/// the contractor's HC4 re-derives them from the linking equalities.
std::vector<int> FiniteDims(const Box& box);

/// Substitution mapping each UNBOUNDED variable that an equality `v == e`
/// defines (in @p formula_evaluators) to its defining expression `e`, with
/// chained CSEs resolved to a fixpoint so every RHS references only bounded
/// variables. Lets COBYLA see the hard constraint as a self-contained function
/// of the bounded primaries. A bounded `v` is never substituted.
ExpressionSubstitution DerivedSubstitution(
    const std::vector<FormulaEvaluator>& formula_evaluators, const Box& box);

/// @p n Latin-hypercube sample points over the @p dims subspace of @p box (each
/// a full-width vector in box-dimension order; non-`dims` entries left 0). Pure
/// geometry — one stratified, jittered permutation per dimension from @p seed,
/// hence deterministic and gradient-free (immune to the flat-center stall).
std::vector<std::vector<double>> LatinHypercubeSamples(
    const Box& box, const std::vector<int>& dims, int n, std::uint32_t seed);

/// Multi-start COBYLA seeds: derivative-free local optimization toward
/// feasibility from the box center plus (`config.seed_samples()` − 1) LHS
/// starts, returning each optimized point (full-width, unbounded dims left 0). A
/// start that throws contributes no candidate (the canonical ICP search is the
/// real path — cache/recompute carve-out, NOT a fallback).
std::vector<std::vector<double>> NloptSeeds(
    const std::vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    const Config& config, const UpwardRounding& ur);

/// A small SOUND box around proposal point @p pt: each FINITE dim pinned to
/// `[pt_i − half_width, pt_i + half_width]` rounded OUTWARD (so the interval
/// soundly contains it) and intersected with @p box (so it stays in-domain);
/// unbounded dims left at full range. @p pt is only a proposal — soundness
/// rides on this construction plus the downstream EvaluateBox.
/// @pre caller holds an UpwardRounding token.
Box SmallBoxAround(const Box& box, const std::vector<double>& pt,
                   double half_width, const UpwardRounding& ur);

}  // namespace dreal
