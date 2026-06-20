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

// FPU rounding-mode discipline — one model, two symmetric regimes.
//
// The process FPU rounding mode is a thread-global resource. dReal has exactly
// two regimes, and *every* mode-sensitive operation belongs to one of them:
//
//   Interval regime  (FE_UPWARD)     gaol/ibex interval arithmetic is sound
//                                    only under FE_UPWARD; under any other mode
//                                    directed rounding inverts and a box can
//                                    silently empty -> a false `unsat`.
//   Nearest regime   (FE_TONEAREST)  decimal formatting, libm, CAPD, NLopt.
//                                    A `double` formatted to decimal under
//                                    FE_UPWARD prints the *wrong number*; a SAT
//                                    model would report a value it never holds.
//
// Both regimes get the identical, symmetric treatment:
//
//                    | Interval regime        | Nearest regime
//   -----------------+------------------------+-------------------------
//   Scope (RAII)     | UpwardRoundingScope    | NearestRoundingScope
//   Token (proof)    | UpwardRounding         | NearestRounding
//   Consumers        | rounded_interval.h     | rounded_format.h,
//                    | (safe_mid/diam, sub_*, | json_guarded.h
//                    |  ibex_hc4_backward)    | (format_double, dump_json),
//                    |                        | math.cc, NLopt, CAPD
//   Backstop (Debug) | DREAL_ASSERT_ROUNDING(FE_UPWARD) / (FE_TONEAREST)
//
// A *Scope establishes its mode once (RAII restore) and is the sole minter of
// its capability token; the token is threaded as a compile-time *proof* of the
// mode into every consumer that performs the regime's mode-sensitive work.  A
// consumer cannot run without a caller-supplied token, and the only minter is
// the matching Scope, so the invariant is compile-time-enforced.  The shared
// primitive `rounding_detail::RoundingModeGuard` is sealed (private ctor): no
// call site outside this header ever names a rounding-mode constant.

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

// Forward declarations: the two scopes are the sole friends of the sealed
// primitive and the sole minters of the two tokens.
class UpwardRoundingScope;
class NearestRoundingScope;

namespace rounding_detail {

#ifndef NDEBUG
// Debug-only shadow of the rounding mode the RoundingModeGuard stack believes
// is installed on this thread. It lets DREAL_ASSERT_ROUNDING_CONSISTENT()
// detect *drift* between what the guards think and what the FPU register
// actually holds — e.g. CAPD's DoubleRounding (or any fesetround that bypasses
// a guard) clobbering the mode behind a guard's back. The sentinel
// kRoundingShadowUnset means "no guard has run yet on this thread".
constexpr int kRoundingShadowUnset = -1;
inline thread_local int g_rounding_shadow = kRoundingShadowUnset;
#endif

// Sealed RAII primitive: save the current rounding-mode, switch, restore on
// destruction. The mode-taking constructor is private; only the two
// *RoundingScope classes may construct one, so establishment always goes
// through a named scope and no call site names a bare rounding-mode constant.
class RoundingModeGuard {
 public:
  RoundingModeGuard(const RoundingModeGuard&) = delete;
  RoundingModeGuard(RoundingModeGuard&&) = delete;
  RoundingModeGuard& operator=(const RoundingModeGuard&) = delete;
  RoundingModeGuard& operator=(RoundingModeGuard&&) = delete;

  /// Destructor. Restore the saved rounding-mode.
  ~RoundingModeGuard() {
    fesetround(round_mode_);
#ifndef NDEBUG
    g_rounding_shadow = round_mode_;
#endif
  }

 private:
  /// Saves the current rounding-mode and switch to @p new_round.
  explicit RoundingModeGuard(int new_round) : round_mode_{fegetround()} {
    fesetround(new_round);
#ifndef NDEBUG
    g_rounding_shadow = new_round;
#endif
  }

  /// Saved rounding-mode at the construction.
  const int round_mode_{};

  friend class ::dreal::UpwardRoundingScope;
  friend class ::dreal::NearestRoundingScope;
};

}  // namespace rounding_detail

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

/// Zero-size capability token proving the FE_UPWARD "interval phase" rounding
/// mode is established on the current thread. gaol/ibex interval arithmetic is
/// sound only under FE_UPWARD; threading this token from the phase entry — the
/// one place that establishes the mode, via UpwardRoundingScope — down through
/// Contractor::Prune and every gaol consumer makes that invariant
/// *compile-time-checked*. Copyable and empty, so passing it by value or const
/// ref is free.
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

/// Zero-size capability token proving the FE_TONEAREST rounding mode is
/// established on the current thread. The exact mirror of UpwardRounding for the
/// nearest regime: decimal formatting (format_double / dump_json), libm math,
/// CAPD and NLopt are correct only under FE_TONEAREST, and threading this token
/// from NearestRoundingScope makes that a compile-time requirement.
class NearestRounding {
 public:
  NearestRounding(const NearestRounding&) = default;
  NearestRounding(NearestRounding&&) = default;
  NearestRounding& operator=(const NearestRounding&) = default;
  NearestRounding& operator=(NearestRounding&&) = default;
  ~NearestRounding() = default;

 private:
  NearestRounding() = default;
  friend class NearestRoundingScope;
};

/// Establishes FE_UPWARD for its lifetime (RAII restore) and is the sole minter
/// of UpwardRounding tokens. Construct one at each entry into an
/// interval-contraction phase (the ICP loop, and anywhere CAPD code must
/// re-establish FE_UPWARD for an inner ibex contractor) and pass token() down.
/// Because it owns a RoundingModeGuard, the fesetround happens once per scope,
/// not once per consumer.
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
  rounding_detail::RoundingModeGuard guard_;
};

/// Establishes FE_TONEAREST for its lifetime (RAII restore) and is the sole
/// minter of NearestRounding tokens. The mirror of UpwardRoundingScope: open one
/// at every entry into a nearest-regime computation (model/result printing,
/// json serialization, libm math, a CAPD/NLopt call, or — when inside an
/// interval phase — a nested nearest computation) and pass token() down.
class NearestRoundingScope {
 public:
  NearestRoundingScope() : guard_{FE_TONEAREST} {}

  NearestRoundingScope(const NearestRoundingScope&) = delete;
  NearestRoundingScope(NearestRoundingScope&&) = delete;
  NearestRoundingScope& operator=(const NearestRoundingScope&) = delete;
  NearestRoundingScope& operator=(NearestRoundingScope&&) = delete;
  ~NearestRoundingScope() = default;

  /// Mints a capability token witnessing that FE_TONEAREST is established.
  NearestRounding token() const { return NearestRounding{}; }

 private:
  rounding_detail::RoundingModeGuard guard_;
};
}  // namespace dreal
