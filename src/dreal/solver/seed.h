/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#include <vector>

#include "dreal/solver/config.h"
#include "dreal/solver/formula_evaluator.h"
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

}  // namespace dreal
