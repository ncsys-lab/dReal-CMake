// Unit test for ode_step_from_name — the --visualize per-segment `step` parser
// (contractor_odes.{h,cc}). It must recognize BOTH BMC-unroller naming schemes:
//   dReach:      <base>_<step>_{0,t}   (e.g. height_3_t -> 3)
//   SMT-LIB BMC: <base>_k<step>        (e.g. x_decay_IntX_k0 -> 0)
// Returning 0 when neither shape is present. See docs/dreal-bugs.md BUG-007.

#include "dreal/contractor/odes/contractor_odes.h"

#include <string>

#include <gtest/gtest.h>

namespace dreal {
namespace {

// dReach convention: <base>_<step>_{0,t}; step is the token between the last
// two underscores.
TEST(OdeStepFromName, DReachConvention) {
  EXPECT_EQ(ode_step_from_name("x_0_0"), 0u);
  EXPECT_EQ(ode_step_from_name("x_0_t"), 0u);
  EXPECT_EQ(ode_step_from_name("x_1_0"), 1u);
  EXPECT_EQ(ode_step_from_name("x_1_t"), 1u);
  EXPECT_EQ(ode_step_from_name("height_3_t"), 3u);
  EXPECT_EQ(ode_step_from_name("x_12_t"), 12u);
  // A base name with embedded underscores still parses the trailing step token.
  EXPECT_EQ(ode_step_from_name("x_decay_IntX_2_0"), 2u);
}

// SMT-LIB BMC convention: <base>_k<step>; step is the integer after "_k".
TEST(OdeStepFromName, BmcConvention) {
  EXPECT_EQ(ode_step_from_name("x_k0"), 0u);
  EXPECT_EQ(ode_step_from_name("x_k1"), 1u);
  EXPECT_EQ(ode_step_from_name("x_k10"), 10u);
  EXPECT_EQ(ode_step_from_name("x_decay_IntX_k0"), 0u);
  EXPECT_EQ(ode_step_from_name("x_decay_IntX_k1"), 1u);
}

// Neither shape present -> 0 (also the legitimate first-step value).
TEST(OdeStepFromName, NoStep) {
  EXPECT_EQ(ode_step_from_name("x"), 0u);
  EXPECT_EQ(ode_step_from_name("mode"), 0u);
  EXPECT_EQ(ode_step_from_name(""), 0u);
  EXPECT_EQ(ode_step_from_name("foo_k"), 0u);   // empty after k
  EXPECT_EQ(ode_step_from_name("foo_"), 0u);    // trailing underscore
  EXPECT_EQ(ode_step_from_name("_k1"), 0u);     // degenerate empty base -> 0
}

}  // namespace
}  // namespace dreal
