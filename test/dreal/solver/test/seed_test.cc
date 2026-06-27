/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0
*/
#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"
#include "dreal/symbolic/symbolic.h"

namespace dreal {
namespace {

// A Config with the --seed-local seed-and-verify pre-pass enabled.
Config SeedConfig(const SeedMethod method) {
  Config c;
  c.mutable_seed_local().set_from_command_line(true);
  c.mutable_seed_method().set_from_command_line(method);
  c.mutable_seed_samples().set_from_command_line(64);
  return c;
}

// A Config with seeding explicitly OFF (seed_local is on by default now), for
// the seeded-vs-unseeded parity baseline.
Config UnseededConfig() {
  Config c;
  c.mutable_seed_local().set_from_command_line(false);
  return c;
}

// Off-center SAT: the geometric center (0,0) VIOLATES x²+y² ≥ 1 (0 ≥ 1 is
// false); the feasible region is the off-center annulus. This is the structure
// --seed-local targets — a fat feasible region that does not contain the center.
// Seeding must reach delta-sat (the same verdict the default search reaches).
class SeedTest : public ::testing::Test {
 protected:
  const Variable x_{"x"};
  const Variable y_{"y"};
};

TEST_F(SeedTest, OffCenterSatLhs) {
  Context ctx{SeedConfig(SeedMethod::kLhs)};
  ctx.DeclareVariable(x_);
  ctx.DeclareVariable(y_);
  ctx.Assert(x_ >= -2);
  ctx.Assert(x_ <= 2);
  ctx.Assert(y_ >= -2);
  ctx.Assert(y_ <= 2);
  ctx.Assert(x_ * x_ + y_ * y_ >= 1);
  EXPECT_TRUE(ctx.CheckSat());
}

TEST_F(SeedTest, OffCenterSatNlopt) {
  Context ctx{SeedConfig(SeedMethod::kNlopt)};
  ctx.DeclareVariable(x_);
  ctx.DeclareVariable(y_);
  ctx.Assert(x_ >= -2);
  ctx.Assert(x_ <= 2);
  ctx.Assert(y_ >= -2);
  ctx.Assert(y_ <= 2);
  ctx.Assert(x_ * x_ + y_ * y_ >= 1);
  EXPECT_TRUE(ctx.CheckSat());
}

// Verdict parity: seeding must not change a verdict, only the search order. The
// same off-center SAT instance is delta-sat with and without seeding.
TEST_F(SeedTest, VerdictParitySat) {
  Context plain{UnseededConfig()};
  plain.DeclareVariable(x_);
  plain.DeclareVariable(y_);
  plain.Assert(x_ >= -2);
  plain.Assert(x_ <= 2);
  plain.Assert(y_ >= -2);
  plain.Assert(y_ <= 2);
  plain.Assert(x_ * x_ + y_ * y_ >= 1);
  EXPECT_TRUE(plain.CheckSat());

  Context seeded{SeedConfig(SeedMethod::kLhs)};
  seeded.DeclareVariable(x_);
  seeded.DeclareVariable(y_);
  seeded.Assert(x_ >= -2);
  seeded.Assert(x_ <= 2);
  seeded.Assert(y_ >= -2);
  seeded.Assert(y_ <= 2);
  seeded.Assert(x_ * x_ + y_ * y_ >= 1);
  EXPECT_TRUE(seeded.CheckSat());
}

// Spurious-candidate SOUNDNESS guard. The empty annulus x²+y² ≤ 0.01 ∧
// x²+y² ≥ 0.04 is UNSAT (radius ≤ 0.1 and ≥ 0.2 cannot both hold), yet sampled
// points NEAR-feasibly satisfy one constraint each (points near the origin
// satisfy the first; points at radius ~0.2 the second). Seeding must NOT
// manufacture a false delta-sat: the box verify (EvaluateBox) rejects every
// candidate, so the verdict stays unsat. (COMPLETENESS-only optimization —
// cannot assert φ T-sat on a T-unsat φ.) If a future change made the seed path
// trust a candidate WITHOUT the box verify, this test would flip to a false
// delta-sat and fail.
TEST_F(SeedTest, SpuriousCandidateStaysUnsatLhs) {
  Context ctx{SeedConfig(SeedMethod::kLhs)};
  ctx.DeclareVariable(x_);
  ctx.DeclareVariable(y_);
  ctx.Assert(x_ >= -2);
  ctx.Assert(x_ <= 2);
  ctx.Assert(y_ >= -2);
  ctx.Assert(y_ <= 2);
  ctx.Assert(x_ * x_ + y_ * y_ <= 0.01);
  ctx.Assert(x_ * x_ + y_ * y_ >= 0.04);
  EXPECT_FALSE(ctx.CheckSat());
}

TEST_F(SeedTest, SpuriousCandidateStaysUnsatNlopt) {
  Context ctx{SeedConfig(SeedMethod::kNlopt)};
  ctx.DeclareVariable(x_);
  ctx.DeclareVariable(y_);
  ctx.Assert(x_ >= -2);
  ctx.Assert(x_ <= 2);
  ctx.Assert(y_ >= -2);
  ctx.Assert(y_ <= 2);
  ctx.Assert(x_ * x_ + y_ * y_ <= 0.01);
  ctx.Assert(x_ * x_ + y_ * y_ >= 0.04);
  EXPECT_FALSE(ctx.CheckSat());
}

// Verdict parity on a plainly UNSAT instance (empty interval intersection).
TEST_F(SeedTest, VerdictParityUnsat) {
  Context ctx{SeedConfig(SeedMethod::kLhs)};
  ctx.DeclareVariable(x_);
  ctx.Assert(x_ >= 0);
  ctx.Assert(x_ <= 1);
  ctx.Assert(x_ * x_ >= 4);  // x ∈ [0,1] ⇒ x² ∈ [0,1], never ≥ 4
  EXPECT_FALSE(ctx.CheckSat());
}

}  // namespace
}  // namespace dreal
