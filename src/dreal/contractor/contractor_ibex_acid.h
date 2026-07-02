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
#include "dreal/contractor/contractor_ibex_polytope.h"  // for ExprCtrDeleter
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/ibex_converter.h"

namespace dreal {

/// Contractor wrapping IBEX's ACID / 3BCID shaving contractor on top of the HC4
/// path. Structurally mirrors ContractorIbexPolytope: it assembles an
/// ibex::System over the box variables and the assertion constraints, then wraps
/// a single ibex::Ctc. The wrapped contractor is
///   - CtcAcid(system, CtcHC4(system))   when config.use_acid(), else
///   - Ctc3BCid(CtcHC4(system))          when config.use_3bcid().
/// ACID is IBEX's own default contractor (DefaultSolver runs HC4 then ACID);
/// dReal otherwise runs HC4 only. This is a COMPLETENESS lever — stronger
/// contraction = fewer search nodes, never a soundness change.
///
/// Prune runs real gaol interval arithmetic (HC4Revise + shaving), so the
/// FE_UPWARD rounding mode is load-bearing — a wrong ambient mode is a silent
/// false `unsat`.
class ContractorIbexAcid : public ContractorCell {
 public:
  /// Constructs an ACID/3BCID contractor over @p formulas and @p box.
  ContractorIbexAcid(std::vector<Formula> formulas, const Box& box,
                     const Config& config);

  /// Deleted copy constructor.
  ContractorIbexAcid(const ContractorIbexAcid&) = delete;

  /// Deleted move constructor.
  ContractorIbexAcid(ContractorIbexAcid&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexAcid& operator=(const ContractorIbexAcid&) = delete;

  /// Deleted move assign operator.
  ContractorIbexAcid& operator=(ContractorIbexAcid&&) = delete;

  /// Default destructor.
  ~ContractorIbexAcid() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor (no constraints).
  bool is_dummy() const;

 private:
  const std::vector<Formula> formulas_;
  bool is_dummy_{false};

  IbexConverter ibex_converter_;
  std::unique_ptr<ibex::SystemFactory> system_factory_;
  std::unique_ptr<ibex::System> system_;
  std::unique_ptr<ibex::CtcHC4> hc4_sub_;  // sub-contractor; outlives ctc_
  std::unique_ptr<ibex::Ctc> ctc_;         // CtcAcid or Ctc3BCid over hc4_sub_
  std::vector<std::unique_ptr<const ibex::ExprCtr, ExprCtrDeleter>> expr_ctrs_;
};

}  // namespace dreal
