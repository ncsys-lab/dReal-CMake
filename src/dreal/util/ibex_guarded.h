/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License").
*/
#pragma once

#include <utility>

#include "ibex.h"

#include "dreal/util/assert.h"
#include "dreal/util/rounding_mode_guard.h"

namespace dreal {

// Token-gated wrappers for raw IBEX/gaol interval arithmetic.
//
// gaol is sound only under FE_UPWARD (see rounding_mode_guard.h). The
// UpwardRounding token is the compile-time proof that the FE_UPWARD interval
// phase is established. Routing every raw ibex arithmetic call through a wrapper
// that *requires* the token means the rounding lint (scripts/rounding_lint.py)
// can forbid any direct ibex::Function::backward / Ctc::contract call: such a
// call would be unable to prove its rounding mode. This closes the gap the
// per-Prune token alone leaves (the token gates Prune entry; these wrappers gate
// the arithmetic itself).

/// IBEX HC4 backward contraction (the forward-backward workhorse). @p cb is the
/// per-variable narrowing callback. Returns true if the box was already inner.
template <typename Callback>
bool ibex_hc4_backward(const ibex::Function& f, const ibex::Domain& rhs,
                       ibex::IntervalVector& box, Callback&& cb,
                       const UpwardRounding& /*ur*/) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  return f.backward(rhs, box, std::forward<Callback>(cb));
}

}  // namespace dreal
