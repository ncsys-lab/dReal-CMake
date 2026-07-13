/*
   Copyright 2026 dReal contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0
*/
#include "dreal/solver/seed/seed.h"

#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"
#include "dreal/solver/formula_evaluator.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/rounding.h"

namespace dreal {
namespace {

using std::vector;

constexpr double kInf{std::numeric_limits<double>::infinity()};

// A Config with the seed-and-verify pre-pass enabled (multi-start COBYLA).
// --seed-samples is the on/off switch: > 0 enables it.
Config SeedConfig() {
  Config c;
  c.mutable_seed_samples().set_from_command_line(64);
  return c;
}

// A Config with seeding explicitly OFF (--seed-samples 0; the count defaults
// to 64, i.e. on), for the seeded-vs-unseeded parity baseline.
Config UnseededConfig() {
  Config c;
  c.mutable_seed_samples().set_from_command_line(0);
  return c;
}

// ===========================================================================
// Pure-geometry units (no nlopt; deterministic). These carry the soundness
// weight (SmallBoxAround's outward rounding) and the LHS invariants.
// ===========================================================================

// FiniteDims selects exactly the both-bounds-finite dimensions. A HALF-OPEN dim
// (one infinite bound) must be excluded — only a fully bounded dim is samplable.
TEST(SeedFiniteDimsTest, SelectsOnlyBothBoundsFinite) {
  const Variable a{"a"}, b{"b"}, c{"c"}, d{"d"};
  Box box{vector<Variable>{a, b, c, d}};
  box[a] = Box::Interval(-1.0, 1.0);    // finite          -> index 0
  box[b] = Box::Interval(-kInf, kInf);  // unbounded
  box[c] = Box::Interval(0.0, kInf);    // half-open (adversarial)
  box[d] = Box::Interval(2.0, 5.0);     // finite          -> index 3
  EXPECT_EQ(FiniteDims(box), (vector<int>{0, 3}));
}

TEST(SeedFiniteDimsTest, NoFiniteDimsIsEmpty) {
  const Variable a{"a"}, b{"b"};
  Box box{vector<Variable>{a, b}};
  box[a] = Box::Interval(-kInf, kInf);
  box[b] = Box::Interval(0.0, kInf);  // half-open
  EXPECT_TRUE(FiniteDims(box).empty());
}

// SmallBoxAround SOUNDNESS: the pinned interval must round OUTWARD so it
// contains [pt - w, pt + w]. With a SUB-ULP half-width, the exact pt ± w falls
// strictly between pt and its float neighbours, so a sound (outward) box puts pt
// in its interior, while an inward-rounding bug would collapse the dim to {pt}
// and FAIL this test (it would no longer contain pt - w).
TEST(SeedSmallBoxAroundTest, OutwardRoundingPutsPointInInterior) {
  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const Variable x{"x"};
  Box box{vector<Variable>{x}};
  box[x] = Box::Interval(-10.0, 10.0);
  const double p{0.1};       // not exactly representable
  const double w{1e-18};     // sub-ULP(0.1) (~1.4e-17) -> rounding direction shows
  const Box b{SmallBoxAround(box, {p}, w, ur)};
  EXPECT_LT(b[x].lb(), p);   // outward-down: strictly below pt
  EXPECT_GT(b[x].ub(), p);   // outward-up:   strictly above pt
  EXPECT_GE(b[x].lb(), -10.0);  // stays in domain
  EXPECT_LE(b[x].ub(), 10.0);
}

// The intersection with the box keeps a near-edge proposal inside the declared
// domain (an outward box could otherwise poke past the upper bound).
TEST(SeedSmallBoxAroundTest, ClipsToDomain) {
  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const Variable x{"x"};
  Box box{vector<Variable>{x}};
  box[x] = Box::Interval(0.0, 1.0);
  const Box b{SmallBoxAround(box, {1.0}, 0.001, ur)};  // pt on the upper edge
  EXPECT_GE(b[x].lb(), 0.0);
  EXPECT_LE(b[x].ub(), 1.0);  // clipped at the domain edge, never beyond
  EXPECT_FALSE(b.empty());
}

// SOUNDNESS guard: a proposal OUTSIDE the domain yields an empty box (the
// intersection is empty), so a wild candidate can never become an in-domain
// model — the downstream EvaluateBox is never even asked about a box that
// escapes the declared bounds.
TEST(SeedSmallBoxAroundTest, PointOutsideDomainEmpties) {
  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const Variable x{"x"};
  Box box{vector<Variable>{x}};
  box[x] = Box::Interval(0.0, 1.0);
  const Box b{SmallBoxAround(box, {5.0}, 0.001, ur)};  // far outside [0,1]
  EXPECT_TRUE(b.empty());
}

// Unbounded (e.g. CSE-aux) dims are LEFT at full range — the contractor's HC4
// re-derives them from the linking equalities; the proposal coordinate for such
// a dim is ignored.
TEST(SeedSmallBoxAroundTest, LeavesUnboundedDimsFullRange) {
  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const Variable x{"x"}, a{"a"};
  Box box{vector<Variable>{x, a}};
  box[x] = Box::Interval(-5.0, 5.0);
  box[a] = Box::Interval(-kInf, kInf);
  const Box b{SmallBoxAround(box, {0.0, 999.0}, 0.001, ur)};  // a's coord ignored
  EXPECT_FALSE(std::isfinite(b[a].lb()));
  EXPECT_FALSE(std::isfinite(b[a].ub()));
  EXPECT_LT(b[x].lb(), b[x].ub());  // x pinned to a small interior box
}

// LHS is deterministic from its seed and never leaves the box.
TEST(SeedLatinHypercubeTest, DeterministicAndInBox) {
  const Variable x{"x"}, y{"y"};
  Box box{vector<Variable>{x, y}};
  box[x] = Box::Interval(-3.0, 5.0);
  box[y] = Box::Interval(0.0, 2.0);
  const int n{16};
  const auto p1{LatinHypercubeSamples(box, {0, 1}, n, 42u)};
  const auto p2{LatinHypercubeSamples(box, {0, 1}, n, 42u)};
  ASSERT_EQ(p1.size(), static_cast<size_t>(n));
  EXPECT_EQ(p1, p2);  // same seed -> identical points
  for (const auto& pt : p1) {
    ASSERT_EQ(pt.size(), 2u);
    EXPECT_GE(pt[0], -3.0);
    EXPECT_LE(pt[0], 5.0);
    EXPECT_GE(pt[1], 0.0);
    EXPECT_LE(pt[1], 2.0);
  }
}

// The defining LHS invariant: across n samples, each of the n equal-width strata
// of a dimension is hit EXACTLY once (a stratified permutation, not i.i.d.
// uniform). Box [0, n] makes the stratum index just floor(coordinate).
TEST(SeedLatinHypercubeTest, OneSamplePerStratumPerDim) {
  const int n{16};
  const Variable x{"x"};
  Box box{vector<Variable>{x}};
  box[x] = Box::Interval(0.0, static_cast<double>(n));
  const auto pts{LatinHypercubeSamples(box, {0}, n, 7u)};
  ASSERT_EQ(pts.size(), static_cast<size_t>(n));
  vector<bool> seen(n, false);
  for (const auto& pt : pts) {
    const int s{static_cast<int>(std::floor(pt[0]))};
    ASSERT_GE(s, 0);
    ASSERT_LT(s, n);
    EXPECT_FALSE(seen[s]) << "stratum " << s << " hit twice";
    seen[s] = true;
  }
  for (int k = 0; k < n; ++k) {
    EXPECT_TRUE(seen[k]) << "stratum " << k << " never hit";
  }
}

// ===========================================================================
// CSE substitution unit (DerivedSubstitution) — the historically bug-prone
// chain-resolution path that the end-to-end x²+y² instances never exercise.
// ===========================================================================

TEST(SeedDerivedSubstitutionTest, MapsUnboundedEqualityVar) {
  const Variable x{"x"}, a{"a"};
  Box box{vector<Variable>{x, a}};
  box[x] = Box::Interval(-2.0, 2.0);
  box[a] = Box::Interval(-kInf, kInf);  // unbounded aux
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(a == x * x)};
  const ExpressionSubstitution s{DerivedSubstitution(fes, box)};
  ASSERT_EQ(s.size(), 1u);
  ASSERT_EQ(s.count(a), 1u);
  EXPECT_TRUE(s.at(a).EqualTo(x * x));
}

// A CSE defined in terms of another CSE must resolve to the FIXPOINT — b's RHS
// references only the bounded primary x, never the intermediate aux a.
TEST(SeedDerivedSubstitutionTest, ResolvesChainedCseToFixpoint) {
  const Variable x{"x"}, a{"a"}, b{"b"};
  Box box{vector<Variable>{x, a, b}};
  box[x] = Box::Interval(-2.0, 2.0);
  box[a] = Box::Interval(-kInf, kInf);
  box[b] = Box::Interval(-kInf, kInf);
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(a == x * x),
      make_relational_formula_evaluator(b == a + 1)};
  const ExpressionSubstitution s{DerivedSubstitution(fes, box)};
  ASSERT_EQ(s.size(), 2u);
  EXPECT_TRUE(s.at(a).EqualTo(x * x));
  EXPECT_TRUE(s.at(b).EqualTo(x * x + 1));  // resolved through a, not 'a + 1'
}

// A BOUNDED variable that an equality defines is NOT a free CSE aux — it carries
// a real domain and must stay a constraint, never be substituted away.
TEST(SeedDerivedSubstitutionTest, BoundedVarNotSubstituted) {
  const Variable x{"x"}, a{"a"};
  Box box{vector<Variable>{x, a}};
  box[x] = Box::Interval(-2.0, 2.0);
  box[a] = Box::Interval(-5.0, 5.0);  // BOUNDED
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(a == x * x)};
  EXPECT_TRUE(DerivedSubstitution(fes, box).empty());
}

// ===========================================================================
// AllRelational gate — the soundness/scoping boundary. Seeding must fire only
// on pure-NRA calls; a regression that let it fire on forall/ODE would pass a
// verdict-only suite but waste work (forall) or be ill-defined (ODE).
// ===========================================================================

TEST(SeedAllRelationalTest, TrueForPlainRelational) {
  const Variable x{"x"}, y{"y"};
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(x + y >= 1),
      make_relational_formula_evaluator(x * x <= 4)};
  EXPECT_TRUE(AllRelational(fes));
}

TEST(SeedAllRelationalTest, FalseWhenForallPresent) {
  const Variable x{"x"}, y{"y"};
  const Formula quantified{forall({y}, x + y >= y)};
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(x >= 0),
      make_forall_formula_evaluator(quantified, 0.01, 0.001, 1)};
  EXPECT_FALSE(AllRelational(fes));
}

TEST(SeedAllRelationalTest, FalseWhenOdePresent) {
  const Variable fx{"flow_x", Variable::Type::CONTINUOUS};
  const Variable x0{"flow_x_0_0", Variable::Type::CONTINUOUS};
  const Variable xt{"flow_x_0_t", Variable::Type::CONTINUOUS};
  const Variable t0{"flow_time_0", Variable::Type::CONTINUOUS};
  const auto ode{std::make_shared<const OdeFlow>(
      "flow", vector<std::pair<Variable, Expression>>{{fx, -fx}})};
  const Formula ic{integral(0.0, t0, {x0}, {xt}, ode)};
  ASSERT_TRUE(ic.include_ode());  // precondition: this really is an ODE formula
  const vector<FormulaEvaluator> fes{
      make_ode_formula_evaluator(ic, /*refine_witness=*/false)};
  EXPECT_FALSE(AllRelational(fes));
}

// ===========================================================================
// SeedBoxes pipeline — exercising the CSE path and the strict-`>` NNF path that
// the verdict-level Context tests cannot distinguish (the complete root search
// reaches the same verdict either way).
// ===========================================================================

// CSE-bearing instance: an unbounded aux `a == x²+y²` plus an off-center
// constraint on the aux. DerivedSubstitution must resolve a -> x²+y² so COBYLA
// optimizes over the bounded primaries; the produced seed boxes must be SOUND
// sub-boxes of the domain (primary dims clipped within their bounds).
TEST(SeedBoxesPipelineTest, CseAuxBoxesAreSoundAndNonEmpty) {
  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const Variable x{"x"}, y{"y"}, a{"a"};
  Box box{vector<Variable>{x, y, a}};
  box[x] = Box::Interval(-2.0, 2.0);
  box[y] = Box::Interval(-2.0, 2.0);
  box[a] = Box::Interval(-kInf, kInf);  // CSE aux, no explicit bound
  const Config cfg{SeedConfig()};
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(a == x * x + y * y),  // CSE definition
      make_relational_formula_evaluator(a >= 1)};             // off-center
  const vector<Box> boxes{SeedBoxes(fes, box, cfg, ur)};
  ASSERT_FALSE(boxes.empty());  // substitution resolved -> COBYLA seeded
  for (const Box& b : boxes) {
    EXPECT_GE(b[x].lb(), -2.0);
    EXPECT_LE(b[x].ub(), 2.0);
    EXPECT_GE(b[y].lb(), -2.0);
    EXPECT_LE(b[y].ub(), 2.0);
  }
}

// NNF push. The real solver hands the theory NEGATED literals: a `>` atom
// assigned false arrives as ¬(≤), and dReal's relational evaluator treats
// ¬(x²+y² ≤ 1) as the constraint x²+y² > 1 (we build that exact form here).
// ConstraintViolation has no signed direction for a raw negation, so without
// the NNF push (Convert(f, true)) the objective is EMPTY, nlopt has nothing to
// minimize, every start throws, and SeedBoxes returns NO boxes. The push
// rewrites ¬(≤) to the positive `>` so a real violation objective is built —
// a non-empty result proves the push fired. (Removing the `true` arg in
// NloptSeeds flips this test red.)
TEST(SeedBoxesPipelineTest, NegatedLiteralProducesSeeds) {
  const UpwardRoundingScope scope;
  const UpwardRounding ur{scope.token()};
  const Variable x{"x"}, y{"y"};
  Box box{vector<Variable>{x, y}};
  box[x] = Box::Interval(-2.0, 2.0);
  box[y] = Box::Interval(-2.0, 2.0);
  const Config cfg{SeedConfig()};
  const vector<FormulaEvaluator> fes{
      make_relational_formula_evaluator(!(x * x + y * y <= 1))};  // ¬(≤) form
  EXPECT_FALSE(SeedBoxes(fes, box, cfg, ur).empty());
}

// ===========================================================================
// End-to-end verdict guards through Context::CheckSat. Seeding may change the
// search order, NEVER a verdict.
// ===========================================================================

// Off-center SAT: the geometric center (0,0) VIOLATES x²+y² ≥ 1 (0 ≥ 1 is
// false); the feasible region is the off-center annulus. This is the structure
// seed-and-verify targets — a fat feasible region that does not contain the
// center.
class SeedTest : public ::testing::Test {
 protected:
  const Variable x_{"x"};
  const Variable y_{"y"};
};

TEST_F(SeedTest, OffCenterSat) {
  Context ctx{SeedConfig()};
  ctx.DeclareVariable(x_);
  ctx.DeclareVariable(y_);
  ctx.Assert(x_ >= -2);
  ctx.Assert(x_ <= 2);
  ctx.Assert(y_ >= -2);
  ctx.Assert(y_ <= 2);
  ctx.Assert(x_ * x_ + y_ * y_ >= 1);
  EXPECT_TRUE(ctx.CheckSat());
}

// Strict `>` off-center SAT — verdict parity for a strict inequality (the
// complete root search reaches delta-sat regardless; this guards that seeding
// does not break the strict-bound path end-to-end).
TEST_F(SeedTest, StrictInequalityOffCenterSat) {
  Context ctx{SeedConfig()};
  ctx.DeclareVariable(x_);
  ctx.DeclareVariable(y_);
  ctx.Assert(x_ >= -2);
  ctx.Assert(x_ <= 2);
  ctx.Assert(y_ >= -2);
  ctx.Assert(y_ <= 2);
  ctx.Assert(x_ * x_ + y_ * y_ > 1);  // STRICT
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

  Context seeded{SeedConfig()};
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
TEST_F(SeedTest, SpuriousCandidateStaysUnsat) {
  Context ctx{SeedConfig()};
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
  Context ctx{SeedConfig()};
  ctx.DeclareVariable(x_);
  ctx.Assert(x_ >= 0);
  ctx.Assert(x_ <= 1);
  ctx.Assert(x_ * x_ >= 4);  // x ∈ [0,1] ⇒ x² ∈ [0,1], never ≥ 4
  EXPECT_FALSE(ctx.CheckSat());
}

}  // namespace
}  // namespace dreal
