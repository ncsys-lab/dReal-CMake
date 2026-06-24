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

    // CAPD rigorous-enclosure C0 set representation, selectable at runtime via
    // --ode-c0-set. Rect2 is a doubleton (x + C*r0 + Q*q) with QR reorganization
    // (CAPD's general-purpose default); Tripleton adds a third reorganization
    // block; HORect2 wraps Rect2 in a Hermite-Obreshkov corrector (tighter, more
    // per-step cost). Maps to capd::C0Rect2Set / C0TripletonSet / C0HORect2Set.
    enum class OdeC0SetType { Rect2, Tripleton, HORect2 };

    // Runtime-tunable CAPD integration knobs, resolved per contractor instance
    // from Config (the Taylor order is direction-specific — fwd vs bwd) and
    // threaded into the run_capd_* adapters. POD, no CAPD dependency, so it can
    // be a by-value member of contractor_ode_lohner and a param of the adapters.
    struct CapdSolverParams {
        int taylor_order;      // capd::IOdeSolver Taylor order for THIS direction
        double abs_tol;        // setAbsoluteTolerance
        double rel_tol;        // setRelativeTolerance
        int hull_grid;         // per-step sub-slices in the tube filter
        OdeC0SetType c0_set;   // rigorous enclosure set representation
        double max_step;       // setMaxStep cap; <= 0 means fully adaptive (no cap)
    };

    class contractor_ode_lohner;  // forward declaration (Codac-based contractor)
}

#endif //DREAL4_CMAKE_ODE_TYPES_H
