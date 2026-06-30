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
#include <vector>

#include "ibex.h"

#include "dreal/contractor/contractor_ibex_polytope.h"  // ExprCtrDeleter
#include "dreal/solver/config.h"                         // SmearVariant
#include "dreal/solver/formula_evaluator.h"
#include "dreal/util/box.h"
#include "dreal/util/dynamic_bitset.h"
#include "dreal/util/ibex_converter.h"
#include "dreal/util/rounding.h"

namespace dreal {

/// Constraint-aware "smear" brancher (IBEX's four SmearFunction variants).
///
/// Picks the split *variable* (not the split point) by a Jacobian-weighted
/// score over the relational constraints. The `SmearVariant` selects one of
/// IBEX's four heuristics along two binary axes — aggregate over constraints
/// by sum vs max, absolute impact |J[i][j]|·diam(box[j]) vs relative
/// (normalized by NC_i = Σ_k |J[i][k]|·diam(box[k])). It assembles an
/// ibex::System from the relational constraints once; because the IbexConverter
/// is built from the Box, the System's variables are in box-index order, so
/// Jacobian column j maps 1:1 to box dimension j (no remapping).
///
/// SOUNDNESS: variable choice can only change how fast the search converges
/// (node count), never a verdict — a completeness/perf lever, never soundness.
/// When the Jacobian is uninformative (all-zero / infinite entries, or no
/// constraints) it falls back to largest-first, matching IBEX's SmearFunction.
/// Not thread-safe to share (operator() mutates ibex Function eval scratch via
/// f_ctrs.jacobian): IcpSeq holds one; IcpParallel builds one per worker.
class SmearBrancher {
 public:
  /// Assembles the constraint system from the relational @p formula_evaluators
  /// over @p box, scoring variables by @p variant. The split point is the
  /// midpoint (Box::bisect default). @p variant must not be kNone (the caller
  /// only constructs a SmearBrancher when smear is enabled).
  SmearBrancher(const std::vector<FormulaEvaluator>& formula_evaluators,
                const Box& box, SmearVariant variant);

  SmearBrancher(const SmearBrancher&) = delete;
  SmearBrancher(SmearBrancher&&) = delete;
  SmearBrancher& operator=(const SmearBrancher&) = delete;
  SmearBrancher& operator=(SmearBrancher&&) = delete;
  ~SmearBrancher() = default;

  /// Brancher interface (same shape as BranchLargestFirst): chooses a dimension
  /// in @p active_set, splits @p box into @p left / @p right at the midpoint, and
  /// returns the dimension (or -1 if none bisectable). Caller must hold FE_UPWARD
  /// (witnessed by @p ur).
  int operator()(const Box& box, const DynamicBitset& active_set, Box* left,
                 Box* right, const UpwardRounding& ur) const;

 private:
  SmearVariant variant_;
  IbexConverter ibex_converter_;
  std::unique_ptr<ibex::SystemFactory> system_factory_;
  std::unique_ptr<ibex::System> system_;
  std::vector<std::unique_ptr<const ibex::ExprCtr, ExprCtrDeleter>> expr_ctrs_;
  bool is_dummy_{false};  // no usable constraints -> always largest-first
};

}  // namespace dreal
