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

#include <ostream>

#include "dreal/contractor/contractor_cell.h"
#include "dreal/contractor/contractor_ibex_forall.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/per_thread.h"

namespace dreal {

/// Multi-thread version of the ContractorIbexForall pre-pruner.
///
/// The base ContractorIbexForall is not thread-safe: its inner ibex::CtcForAll
/// holds a mutable parameter-box worklist (and drives a shared inner System /
/// CtcFwdBwd), so a single cell cannot be shared across parallel ICP workers.
/// When there are N jobs this builds N ContractorIbexForall instances internally
/// and makes sure each thread calls a designated instance. Mirrors
/// ContractorIbexFwdbwdMt / ContractorIbexPolytopeMt.
class ContractorIbexForallMt : public ContractorCell {
 public:
  /// Deleted default constructor.
  ContractorIbexForallMt() = delete;

  /// Constructs IbexForallMt contractor using @p f and @p box.
  ContractorIbexForallMt(Formula f, const Box& box, const Config& config);

  /// Deleted copy constructor.
  ContractorIbexForallMt(const ContractorIbexForallMt&) = delete;

  /// Deleted move constructor.
  ContractorIbexForallMt(ContractorIbexForallMt&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexForallMt& operator=(const ContractorIbexForallMt&) = delete;

  /// Deleted move assign operator.
  ContractorIbexForallMt& operator=(ContractorIbexForallMt&&) = delete;

  ~ContractorIbexForallMt() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;

  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor.
  bool is_dummy() const;

 private:
  ContractorIbexForall* GetCtcOrCreate(const Box& box) const;

  const Formula f_;
  bool is_dummy_{false};
  const Config config_;

  // One ContractorIbexForall per worker thread (the base is not thread-safe).
  mutable PerThread<ContractorIbexForall> ctcs_;
};

}  // namespace dreal
