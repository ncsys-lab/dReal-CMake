/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License").
*/
#pragma once

#include <string>

#include "nlohmann/json.hpp"

#include "dreal/util/assert.h"
#include "dreal/util/rounding.h"

namespace dreal {

// Nearest-regime (FE_TONEAREST) token consumer: json serialization.
//
// nlohmann serializes its stored `double`s to decimal in dump(), which is
// correct only under FE_TONEAREST (the same hazard as format_double — the ODE
// --visualize trajectory enclosures would otherwise mis-print). nlohmann is
// external, so the token gates *our* boundary, exactly like ibex_hc4_backward
// gates the boundary to ibex. The parameters mirror nlohmann::json::dump.
inline std::string dump_json(const nlohmann::json& j,
                             const NearestRounding& /*nr*/, int indent = -1,
                             char indent_char = ' ', bool ensure_ascii = false) {
  DREAL_ASSERT_ROUNDING(FE_TONEAREST);
  return j.dump(indent, indent_char, ensure_ascii);
}

}  // namespace dreal
