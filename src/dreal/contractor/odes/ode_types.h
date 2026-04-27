//
// Created by Kunal Sheth on 9/8/25.
// Updated for Codac migration: removed CAPD dependency.
//

#ifndef DREAL4_CMAKE_ODE_TYPES_H
#define DREAL4_CMAKE_ODE_TYPES_H

#include <utility>
#include <vector>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"

namespace dreal
{
    // An ode_constraint pairs an Integral formula (the ODE definition) with a
    // list of ForallT formulas (the invariants that must hold along the trajectory).
    using ode_constraint = std::pair<Formula, std::vector<Formula>>;

    enum class ode_direction { FWD, BWD };

    class contractor_ode_lohner;  // forward declaration (Codac-based contractor)
}

#endif //DREAL4_CMAKE_ODE_TYPES_H
