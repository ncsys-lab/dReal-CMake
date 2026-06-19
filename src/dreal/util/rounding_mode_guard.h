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

#include <cfenv>
#pragma STDC FENV_ACCESS ON

#include "dreal/util/assert.h"

namespace dreal {

// DREAL_ASSERT_ROUNDING(x): assert the live FPU rounding mode equals x.
//
// Unlike DREAL_ASSERT(fegetround() == x), this compiles to *nothing* under
// NDEBUG — fegetround() is not even called. That matters because these are
// meant to be sprinkled densely at gaol/CAPD boundaries (including hot paths),
// and `unused(fegetround() == x)` would still execute the fegetround() read in
// release builds.
#ifdef NDEBUG
#define DREAL_ASSERT_ROUNDING(x) ((void)0)
#else
#define DREAL_ASSERT_ROUNDING(x) DREAL_ASSERT(fegetround() == (x))
#endif

#ifndef NDEBUG
namespace rounding_detail {
// Debug-only shadow of the rounding mode the RoundingModeGuard stack believes
// is installed on this thread. It lets DREAL_ASSERT_ROUNDING_CONSISTENT()
// detect *drift* between what the guards think and what the FPU register
// actually holds — e.g. CAPD's DoubleRounding (or any fesetround that bypasses
// a guard) clobbering the mode behind a guard's back. The sentinel
// kRoundingShadowUnset means "no guard has run yet on this thread".
constexpr int kRoundingShadowUnset = -1;
inline thread_local int g_rounding_shadow = kRoundingShadowUnset;
}  // namespace rounding_detail
#endif

// DREAL_ASSERT_ROUNDING_CONSISTENT(): assert the live FPU mode still matches
// what the guard stack expects (i.e. nothing changed it outside a guard). Use
// at gaol<->CAPD boundaries to catch an un-guarded clobber. No-op in release,
// and a no-op before the first guard runs on a thread.
#ifdef NDEBUG
#define DREAL_ASSERT_ROUNDING_CONSISTENT() ((void)0)
#else
#define DREAL_ASSERT_ROUNDING_CONSISTENT()                       \
  DREAL_ASSERT(::dreal::rounding_detail::g_rounding_shadow ==             \
                   ::dreal::rounding_detail::kRoundingShadowUnset ||      \
               fegetround() == ::dreal::rounding_detail::g_rounding_shadow)
#endif

class RoundingModeGuard {
 public:
  /// Saves the current rounding-mode and switch to @p new_round.
  explicit RoundingModeGuard(int new_round) : round_mode_{fegetround()} {
    fesetround(new_round);
#ifndef NDEBUG
    rounding_detail::g_rounding_shadow = new_round;
#endif
  }

  /// Deleted Copy-constructor.
  RoundingModeGuard(const RoundingModeGuard&) = delete;

  /// Deleted Move-constructor.
  RoundingModeGuard(RoundingModeGuard&&) = delete;

  /// Deleted Copy-assignment.
  RoundingModeGuard& operator=(const RoundingModeGuard&) = delete;

  /// Deleted Move-assignment.
  RoundingModeGuard& operator=(RoundingModeGuard&&) = delete;

  /// Destructor. Restore the saved rounding-mode.
  ~RoundingModeGuard() {
    fesetround(round_mode_);
#ifndef NDEBUG
    rounding_detail::g_rounding_shadow = round_mode_;
#endif
  }

 private:
  /// Saved rounding-mode at the construction.
  const int round_mode_{};
};

class UpwardRoundingScope;

/// Zero-size capability token proving the FE_UPWARD "interval phase" rounding
/// mode is established on the current thread. gaol/ibex interval arithmetic is
/// sound only under FE_UPWARD; threading this token from the phase entry — the
/// one place that establishes the mode, via UpwardRoundingScope — down through
/// Contractor::Prune makes that invariant *compile-time-checked*: a contractor
/// cannot be pruned without a caller-supplied proof the mode is set, and the
/// only minter is UpwardRoundingScope. Copyable and empty, so passing it by
/// value or const ref is free.
class UpwardRounding {
 public:
  UpwardRounding(const UpwardRounding&) = default;
  UpwardRounding(UpwardRounding&&) = default;
  UpwardRounding& operator=(const UpwardRounding&) = default;
  UpwardRounding& operator=(UpwardRounding&&) = default;
  ~UpwardRounding() = default;

 private:
  UpwardRounding() = default;
  friend class UpwardRoundingScope;
};

/// Establishes FE_UPWARD for its lifetime (RAII restore, via RoundingModeGuard)
/// and is the sole minter of UpwardRounding tokens. Construct one at each entry
/// into an interval-contraction phase (the ICP loop, and anywhere CAPD code
/// must re-establish FE_UPWARD for an inner ibex contractor) and pass token()
/// down. Because it owns a RoundingModeGuard, the expensive fesetround happens
/// once per scope, not once per Prune.
class UpwardRoundingScope {
 public:
  UpwardRoundingScope() : guard_{FE_UPWARD} {}

  UpwardRoundingScope(const UpwardRoundingScope&) = delete;
  UpwardRoundingScope(UpwardRoundingScope&&) = delete;
  UpwardRoundingScope& operator=(const UpwardRoundingScope&) = delete;
  UpwardRoundingScope& operator=(UpwardRoundingScope&&) = delete;
  ~UpwardRoundingScope() = default;

  /// Mints a capability token witnessing that FE_UPWARD is established.
  UpwardRounding token() const { return UpwardRounding{}; }

 private:
  RoundingModeGuard guard_;
};
}  // namespace dreal
