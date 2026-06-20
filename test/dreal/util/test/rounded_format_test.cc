/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License").
*/
#include "dreal/util/rounded_format.h"

#include <cfenv>
#include <limits>
#include <sstream>

#include <gtest/gtest.h>

#include "dreal/util/rounding.h"

namespace dreal {
namespace {

// format_double is the nearest-regime mirror of safe_mid/safe_diam: it formats a
// double to decimal, which is correct only under FE_TONEAREST. The NearestRounding
// token (mintable only by a NearestRoundingScope, i.e. only under FE_TONEAREST)
// is the compile-time proof — there is no way to *call* format_double under
// FE_UPWARD, so the wrong-printed-number bug is unrepresentable, not merely
// asserted. This test pins the wrapper's output to the standard nearest stream.
TEST(RoundedFormatTest, MatchesStandardNearestStream) {
  // Values whose shortest round-trip decimal is not FP-exact (0.1, 0.3, 3.3 are
  // the kinds of constants that mis-round under directed FPU modes).
  for (const double v : {0.1, 0.3, 3.3, -2.7, 1.0 / 3.0, 123456.789}) {
    std::ostringstream ref;
    ref.precision(std::numeric_limits<double>::max_digits10 + 2);
    {
      // Reference rendering under the (default) nearest mode.
      const NearestRoundingScope ref_scope;
      ref << v;
    }

    std::ostringstream got;
    got.precision(std::numeric_limits<double>::max_digits10 + 2);
    const NearestRoundingScope scope;
    format_double(got, v, scope.token());

    EXPECT_EQ(got.str(), ref.str()) << "value = " << v;
  }
}

// format_double restores the prior FPU mode via its NearestRoundingScope's RAII
// guard: opening one inside an FE_UPWARD phase formats under nearest, then leaves
// FE_UPWARD intact (the nested-regime pattern the printers rely on).
TEST(RoundedFormatTest, NestsInsideUpwardPhaseAndRestores) {
  const UpwardRoundingScope phase;
  ASSERT_EQ(fegetround(), FE_UPWARD);
  {
    std::ostringstream oss;
    const NearestRoundingScope nearest;
    EXPECT_EQ(fegetround(), FE_TONEAREST);
    format_double(oss, 0.1, nearest.token());
  }
  EXPECT_EQ(fegetround(), FE_UPWARD);
}

}  // namespace
}  // namespace dreal
