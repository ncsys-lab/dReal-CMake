/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License").
*/
#pragma once

#include <ostream>

#include "dreal/util/assert.h"
#include "dreal/util/rounding.h"

namespace dreal {

// Nearest-regime (FE_TONEAREST) token consumer: decimal formatting of a double.
//
// A double -> decimal conversion is correct only under FE_TONEAREST. Under
// FE_UPWARD the conversion mis-rounds, so a SAT-model value would be printed as
// a number the model does not actually hold. The NearestRounding token is the
// compile-time proof of the mode (the nearest-regime mirror of safe_mid/
// safe_diam's UpwardRounding); the assert is the Debug drift backstop.
//
// This is a token-gated drop-in for a bare `os << v`: the stream's precision —
// set by the caller (PrecisionGuard, PrefixPrinter) — still governs the digit
// count, so output is byte-for-byte unchanged.
inline std::ostream& format_double(std::ostream& os, double v,
                                   const NearestRounding& /*nr*/) {
  DREAL_ASSERT_ROUNDING(FE_TONEAREST);
  return os << v;
}

}  // namespace dreal
