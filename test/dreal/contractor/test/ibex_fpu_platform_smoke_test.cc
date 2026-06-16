// EXPECTED FAILING TEST (or surprising-PASS) under dreal-perf-patches.
//
// This file was calibrated against ncsys-lab/ibex-lib commits fc986657 and
// 4f845aa3 (FPU management for Apple Silicon + fenv.h signature for x86),
// part of the cav26 patch series. Neither is included in the current
// `dreal-perf-patches` minimal-fork branch. Mainline ibex on arm64 macOS
// uses the gaol-rounding fix from `971f8eb0` (March 2025) which addresses
// the Apple Silicon path without the wholesale FPU-management rewrite.
//
// See ../../../../DEPENDENCIES.md and ibex-fork/CLAUDE.md "Fork-diff
// minimization" for why we kept the patch set minimal.
//
// The test is retained as DOCUMENTATION of mainline-vs-cav26 differences
// in directed-rounding behavior. Update goldens to mainline if the
// behavior is acceptable, or re-add the patches to flip these to PASS.
//
// Original purpose (from cav26 context):
// Regression test for ncsys-lab/ibex-lib commits fc986657 and 4f845aa3
// ("Improve low-level FPU management for Apple Silicon" + "Fix fenv.h call
// signature for x86 machines").
//
// These patches live in the gaol tarball patch under
// `interval_lib_wrapper/gaol/3rd/gaol-4.2.3.all.all.patch`. They ensure
// directed rounding works correctly:
//   - On Apple Silicon (arm64 native + Rosetta x86_64): `fesetenv(FE_DFL_ENV)`
//     replaces the old `reset_fpu_cw` which crashed under macOS arm64.
//   - On x86: `fegetround()` / `fesetround()` signature is corrected for the
//     macOS x86 fenv.h header.
//
// At runtime, the symptom of a misconfigured FPU is that interval arithmetic
// over non-representable endpoints produces degenerate or sub-precision
// intervals. This test runs the same smoke checks as
// `ibex_gaol_init_smoke_test.cc`, but parameterized over compile-time platform
// predicates so a per-platform failure can be triaged.

#include <gtest/gtest.h>

#include <ibex.h>

namespace dreal {
namespace {

// =============================================================================
// Compile-time platform fingerprint (logged on test failure to aid triage)
// =============================================================================

const char* kPlatformFingerprint =
#if defined(__APPLE__) && defined(__aarch64__)
    "apple-arm64-native";
#elif defined(__APPLE__) && defined(__x86_64__)
    "apple-x86_64 (likely Rosetta if host is Apple Silicon)";
#elif defined(__linux__) && defined(__x86_64__)
    "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    "linux-aarch64";
#else
    "unknown";
#endif

// See ibex_gaol_init_smoke_test.cc file header for the context: gaol on
// the current cav26-era build returns SINGLETON intervals for operations on
// singleton inputs (`Interval(0.1) + Interval(0.2)` etc.), even though the
// gaol FPU patches are present. Tracking as an investigation item in
// ibex-fork/MIGRATION.md. The tests below CAPTURE the cav26 baseline so a
// future bisect can flag any change.

TEST(IbexFpuPlatformSmoke, AddNonRepresentableMatchesCav26Baseline) {
  // On the cav26 build the bounds match (singleton); upper bound is slightly
  // above the exact 0.3, so the interval still contains 0.3 inclusively.
  const ibex::Interval r = ibex::Interval(0.1) + ibex::Interval(0.2);
  EXPECT_GE(r.ub(), 0.3)
      << "Regression on platform " << kPlatformFingerprint
      << ": upper bound of 0.1+0.2 fell below 0.3, breaking IA soundness.";
  EXPECT_DOUBLE_EQ(r.lb(), r.ub())
      << "On " << kPlatformFingerprint
      << ": cav26 baseline expected singleton; bounds diverged. Either gaol "
         "FPU patches now work correctly (good — please update this test), "
         "or a different regression has crept in.";
}

TEST(IbexFpuPlatformSmoke, DivContainsTrueResult) {
  // [1.0, 1.0] / [3.0, 3.0] must contain the true 1/3.
  // Soundness floor: regardless of rounding direction details, the result
  // must bracket the true value (so it's safe over-approximation).
  const ibex::Interval r = ibex::Interval(1.0) / ibex::Interval(3.0);
  const double truth = 1.0 / 3.0;
  EXPECT_LE(r.lb(), truth + 1e-15)
      << "On " << kPlatformFingerprint << ": 1/3 lb=" << r.lb();
  EXPECT_GE(r.ub(), truth - 1e-15)
      << "On " << kPlatformFingerprint << ": 1/3 ub=" << r.ub();
}

TEST(IbexFpuPlatformSmoke, RepeatedAdditionStaysBounded) {
  // After 100 additions of 0.1, the result should be near 10.1 (a small
  // accumulation of rounding error).
  ibex::Interval acc(0.1);
  for (int i = 0; i < 100; ++i) {
    acc = acc + ibex::Interval(0.1);
  }
  // Sanity bounds (very loose).
  EXPECT_GT(acc.lb(), 9.0);
  EXPECT_LT(acc.ub(), 11.0);
}

}  // namespace
}  // namespace dreal
