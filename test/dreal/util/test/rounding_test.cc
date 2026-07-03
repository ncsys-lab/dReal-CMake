/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License").
*/
// Direct unit tests for the RoundingModeGuard mechanism in util/rounding.h:
//   - the live-based restore (a clean scope restores the caller's mode),
//   - the ExpectClobber tag (suppress the dtor tripwire for a known clobberer
//     AND heal the mode), in both regimes, and
//   - the always-on clobber tripwire itself (an uncontained out-of-band
//     fesetround inside a non-ExpectClobber scope aborts at scope exit).
//
// These cover the mechanism directly; the CAPD adapters (contractor_capd_test)
// and model printing (box_test) cover it through real clobberers.

#include "dreal/util/rounding.h"

#include <cfenv>

#include <gtest/gtest.h>

namespace dreal {
namespace {

class RoundingModeGuardTest : public ::testing::Test {
 protected:
  // Leave the FPU in the process-default nearest mode after each case so a
  // directly-manipulated mode never leaks into a later test.
  void TearDown() override { std::fesetround(FE_TONEAREST); }
};

// A clean scope (nothing changes the mode out-of-band) establishes its regime
// for the body and restores the caller's mode on exit — without aborting.
TEST_F(RoundingModeGuardTest, CleanScopeEstablishesAndRestores) {
  std::fesetround(FE_UPWARD);  // caller's mode
  {
    const NearestRoundingScope g;
    EXPECT_EQ(std::fegetround(), FE_TONEAREST);  // established for the body
  }
  EXPECT_EQ(std::fegetround(), FE_UPWARD);  // caller's mode restored
}

// ExpectClobber: a body that legitimately leaves the mode changed (the CAPD /
// ibex-operator<< case) does NOT trip the tripwire, and the live-based restore
// still heals back to the caller's mode. Nearest regime.
TEST_F(RoundingModeGuardTest, NearestExpectClobberContainsAndHeals) {
  std::fesetround(FE_TONEAREST);  // caller's mode
  {
    const NearestRoundingScope g{expect_clobber};
    std::fesetround(FE_UPWARD);  // a clobber the body leaves behind
  }  // dtor: tripwire suppressed, mode force-restored
  EXPECT_EQ(std::fegetround(), FE_TONEAREST);
}

// The mirror for the interval regime.
TEST_F(RoundingModeGuardTest, UpwardExpectClobberContainsAndHeals) {
  std::fesetround(FE_UPWARD);  // caller's mode
  {
    const UpwardRoundingScope g{expect_clobber};
    std::fesetround(FE_TONEAREST);  // a clobber the body leaves behind
  }
  EXPECT_EQ(std::fegetround(), FE_UPWARD);
}

// Death tests: an out-of-band mode change inside a NON-ExpectClobber scope must
// abort at scope exit via the always-on tripwire — the soundness backstop (a
// wrong ambient mode silently empties a gaol box -> false `unsat`). Re-exec
// ("threadsafe") because the test binary links the full solver.
class RoundingModeGuardDeathTest : public ::testing::Test {
 protected:
  void SetUp() override { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
  void TearDown() override { std::fesetround(FE_TONEAREST); }
};

TEST_F(RoundingModeGuardDeathTest, NearestUncontainedClobberAborts) {
  EXPECT_DEATH(
      {
        const NearestRoundingScope g;
        std::fesetround(FE_UPWARD);  // out-of-band, not expected
      },
      "clobbered out-of-band");
}

TEST_F(RoundingModeGuardDeathTest, UpwardUncontainedClobberAborts) {
  EXPECT_DEATH(
      {
        const UpwardRoundingScope g;
        std::fesetround(FE_TONEAREST);  // out-of-band, not expected
      },
      "clobbered out-of-band");
}

}  // namespace
}  // namespace dreal
