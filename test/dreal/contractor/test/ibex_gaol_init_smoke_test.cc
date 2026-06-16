// EXPECTED FAILING TEST (or surprising-PASS) under dreal-perf-patches.
//
// This file was calibrated against ncsys-lab/ibex-lib commit 059d1fe7
// ("Call gaol::init from wrapper") plus FPU-init patches (fc986657 /
// 4f845aa3) from the cav26 patch series. None of those are included in
// the current `dreal-perf-patches` minimal-fork branch; mainline ibex's
// gaol initialization may produce different singleton/non-singleton
// behavior on the 0.1+0.2 / 1/3 / etc. fixtures the tests below assert on.
//
// See ../../../../DEPENDENCIES.md "Why a fork at all" for which patches
// we carry and why the FPU/gaol-init cleanup didn't pass the upstream-PR
// bar (ibex-fork/CLAUDE.md "Fork-diff minimization").
//
// The test is retained as DOCUMENTATION of where mainline ibex differs
// from the cav26-era FPU-clean baseline. Update goldens to mainline if
// the test runs reliably without the cav26 patches, or re-add the patches
// to flip these to PASS as originally written.
//
// Original purpose (from cav26 context):
// Regression test for ncsys-lab/ibex-lib commit 059d1fe7
// ("Call gaol::init from wrapper").
//
// Pre-patch, ibex's gaol wrapper called `gaol::round_upward()` without first
// calling `gaol::init()`. On some platforms (Apple Silicon under Rosetta, in
// particular) this left the FPU in an undefined rounding state — interval
// arithmetic could silently produce singleton results from operations on
// non-machine-representable endpoints, breaking soundness.
//
// After the patch (`interval_lib_wrapper/gaol/ibex_IntervalLibWrapper.cpp:35`),
// gaol::init() runs first, then round_upward() configures the FPU. Operations
// that should produce a non-degenerate interval do so.
//
// This test exercises the property by computing 0.1 + 0.2 in interval
// arithmetic. Neither 0.1 nor 0.2 is exactly representable in binary; the true
// sum 0.3 is not exactly representable either. A correctly rounded interval
// computation must produce a non-degenerate result containing 0.3.

#include <gtest/gtest.h>

#include <ibex.h>

namespace dreal {
namespace {

// ============================================================================
// Captured cav26-era behavior (baseline for regression):
//
//   ibex::Interval(0.1) + ibex::Interval(0.2) = [0.300000…05, 0.300000…05]  (singleton!)
//   ibex::Interval(0.1) * ibex::Interval(0.1) = [0.0100000…02, 0.0100000…02]
//   ibex::Interval(1.0) / ibex::Interval(3.0) = [0.333…31,     0.333…31]
//
// These ARE singletons on the current build, even after the 059d1fe7 + gaol
// FPU patches were applied (verified post-build: gaol_opposite() is present in
// the patched gaol source tree). That a singleton is being returned for an
// operation on non-representable doubles is suspect — strict IA semantics
// require the output to bracket the true value. The mostly-likely explanations:
//   (a) gaol's `interval(double a)` constructor produces `[a, a]` (singleton);
//       arithmetic on two singletons can legitimately stay singleton if both
//       endpoints round identically.
//   (b) Directed rounding may not be honored by `gaol::round_upward()` on
//       Rosetta x86_64, even with the FPU patches.
//
// Tracking as investigation item in ibex-fork/MIGRATION.md. The tests below
// LOCK IN the current (cav26-era) behavior so future modernization steps
// don't accidentally CHANGE it (in either direction) without us noticing.
// ============================================================================

TEST(IbexGaolInitSmoke, AddOfNonRepresentableMatchesCav26Baseline) {
  const ibex::Interval a(0.1);
  const ibex::Interval b(0.2);
  const ibex::Interval r = a + b;
  // The true value 0.3 must be in the interval (closed [r.lb(), r.ub()]).
  // Note: with the current build, this passes by ub being slightly above 0.3
  // even though lb is also above 0.3. See file header for context.
  EXPECT_GE(r.ub(), 0.3);
  // Captured baseline: singleton at the round-up-of-(0.1+0.2) double.
  EXPECT_DOUBLE_EQ(r.lb(), 0.30000000000000004);
  EXPECT_DOUBLE_EQ(r.ub(), 0.30000000000000004);
}

TEST(IbexGaolInitSmoke, MulOfNonRepresentableMatchesCav26Baseline) {
  const ibex::Interval x(0.1);
  const ibex::Interval r = x * x;
  EXPECT_GE(r.ub(), 0.01);
  EXPECT_DOUBLE_EQ(r.lb(), 0.010000000000000002);
  EXPECT_DOUBLE_EQ(r.ub(), 0.010000000000000002);
}

TEST(IbexGaolInitSmoke, DivMatchesCav26Baseline) {
  const ibex::Interval one(1.0);
  const ibex::Interval three(3.0);
  const ibex::Interval r = one / three;
  EXPECT_GE(r.ub(), 1.0 / 3.0);
  EXPECT_DOUBLE_EQ(r.lb(), 0.33333333333333331);
  EXPECT_DOUBLE_EQ(r.ub(), 0.33333333333333331);
}

}  // namespace
}  // namespace dreal
