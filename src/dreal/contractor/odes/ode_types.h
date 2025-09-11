//
// Created by Kunal Sheth on 9/8/25.
//

#ifndef DREAL4_CMAKE_ODE_TYPES_H
#define DREAL4_CMAKE_ODE_TYPES_H
#include <utility>

#include "dreal/symbolic/symbolic.h"
#include "capd/capdlib.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"

namespace dreal
{
    using ode_constraint = std::pair<Formula, std::vector<Formula>>;

    enum class ode_direction { FWD, BWD };

    class contractor_capd_full;
}

#endif //DREAL4_CMAKE_ODE_TYPES_H
