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

#include "ibex.h"

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_cell.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/ibex_converter.h"

namespace dreal {

// Custom deleter for ibex::ExprCtr (deletes the ExprNode but keeps the
// ExprSymbols; the symbols are freed by IbexConverter's destructor). Same shape
// as ContractorIbexPolytope's ExprCtrDeleter, kept local to avoid coupling the
// two contractor headers.
struct ForallExprCtrDeleter {
  void operator()(const ibex::ExprCtr* const p) const {
    if (p) {
      ibex::cleanup(p->e, false);
      delete p;
    }
  }
};

/// A sound, COMPLETENESS-only pre-pruner for a `∀`-constraint, built on IBEX's
/// native `ibex::CtcForAll` (proj-intersection). It runs *alongside* — never
/// instead of — the δ-complete CEGIS `ContractorForall`: a single pass is pure
/// interval contraction (no nested δ-solve) that shrinks the existential box so
/// CEGIS converges in fewer counterexample iterations.
///
/// Soundness: `CtcForAll` removes an existential point only when the body fails
/// at a *real* universal point `mid(y) ∈ y_init`, and `y_init ⊆` the binder
/// domain by construction (see the .cc), so it can never delete a true ∃∀
/// solution (false `unsat` impossible). The implication guard `domain ⟹ φ` is
/// preserved structurally by the recursive Formula→Ctc builder (`∨`→`CtcUnion`),
/// which is what keeps the `domain`-false region vacuous.
class ContractorIbexForall : public ContractorCell {
 public:
  /// Constructs the pre-pruner for the `∀`-formula @p f over the existential
  /// @p box. If @p f's body is not buildable into IBEX contractors (e.g. a
  /// non-relational leaf, or a universal var with no finite binder bound),
  /// `is_dummy()` is set and the contractor is a no-op — CEGIS still decides.
  ContractorIbexForall(Formula f, const Box& box, const Config& config);

  /// Deleted copy/move constructors and assignments.
  ContractorIbexForall(const ContractorIbexForall&) = delete;
  ContractorIbexForall(ContractorIbexForall&&) = delete;
  ContractorIbexForall& operator=(const ContractorIbexForall&) = delete;
  ContractorIbexForall& operator=(ContractorIbexForall&&) = delete;

  /// Default destructor.
  ~ContractorIbexForall() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if no internal IBEX contractor was built (a no-op).
  bool is_dummy() const;

 private:
  // Pass 1: DFS the (NNF) body, adding every relational-atom leaf to
  // `system_factory_` (so the leaves share one variable space — IBEX forbids a
  // symbol belonging to two Functions). Returns false on a non-buildable leaf.
  bool CollectLeaves(const Formula& f);

  // Pass 2: DFS the same body in the same order, materializing the Ctc tree —
  // conjunction→CtcCompo, disjunction→CtcUnion, atom→CtcFwdBwd(system, i) — with
  // `*next_leaf` indexing the System constraints in DFS order. Each created Ctc
  // is owned by `owned_ctcs_` so it outlives `forall_ctc_`.
  ibex::Ctc* Materialize(const Formula& f, int* next_leaf);

  const Formula f_;
  bool is_dummy_{false};

  // Converter over the joint variable array [existential..., universal...].
  IbexConverter ibex_converter_;

  // The leaves share one System's variable space (the polytope pattern).
  std::unique_ptr<ibex::SystemFactory> system_factory_;
  std::unique_ptr<ibex::System> system_;
  std::unique_ptr<ibex::BitSet> variable_bits_;         // existential indices
  std::vector<std::unique_ptr<ibex::Ctc>> owned_ctcs_;  // leaves + composites
  std::unique_ptr<ibex::CtcForAll> forall_ctc_;
  // Kept alive last (destroyed first); the System copied them on construction.
  std::vector<std::unique_ptr<const ibex::ExprCtr, ForallExprCtrDeleter>>
      expr_ctrs_;
};

}  // namespace dreal
