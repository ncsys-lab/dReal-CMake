// Surgical unit tests for the explanation-soundness fix (the `--constraint-order`
// false-`unsat` bug). FULL WRITE-UP: docs/constraint-order-explanation-soundness.md
//
// THE BUG (SOUNDNESS — false `unsat`). When CAPD diverges, the Lohner contractor
// records no narrowing, so an ODE `integral` that is logically responsible for a
// conflict never reaches `used_constraints_`. The learned theory clause `¬E` is
// then built from the δ-satisfiable relational remainder and forbids a real model.
//
// THE FIX. The contractor's inconclusive exits call `AddInconclusiveOde(ic)`, and
// `GenerateExplanation` splices those ODEs into the explanation as NON-EXPANDING
// leaves keyed on the emptying `unsat_witness` (not the broad relational closure
// `seen`, which bloated lemmas and regressed a c2e2 SAT instance to timeout).
//
// Two layers, same bug:
//   * UNIT (microseconds) — exercise the splice DIRECTLY through `ContractorStatus`'s
//     public API: seed the witness exactly as a refuting `Prune` does (empty the box,
//     then `AddUsedConstraint`), register inconclusive ODEs, inspect `Explanation()`.
//     No solver/ICP/CAPD; the splice keys only on free variables + set membership, so
//     plain relational formulas faithfully stand in for ODE `integral` literals.
//   * INTEGRATION (~3 s) — the real path end-to-end on an INLINE SMT2 string (no
//     external file): a 5-step C2E2-automaton BMC with the flows reduced to a stiff
//     scalar ODE `v'=1e6*v^2` that makes CAPD diverge everywhere. Pre-fix,
//     `--constraint-order desc` returned a false `unsat`; the fix restores the
//     witnessed delta-sat. See the NOTE at that test on why it can't be smaller.

#include "dreal/contractor/contractor_status.h"

#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/smt2/driver.h"
#include "dreal/solver/config.h"
#include "dreal/solver/context.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

// A box whose variables all have finite (non-empty) intervals, so the
// ContractorStatus precondition (!box.empty()) holds at construction.
Box BoxOf(const std::vector<Variable>& vars) {
  Box box;
  for (const Variable& v : vars) box.Add(v, -10.0, 10.0);
  return box;
}

// Seed the unsat witness the way a refuting Prune does: empty the box, then record
// the constraint that emptied it (AddUsedConstraint seeds the witness with the
// formula's free variables iff the box is empty).
void RecordEmptyingConstraint(ContractorStatus* cs, const Formula& f) {
  cs->mutable_box().set_empty();
  cs->AddUsedConstraint(f);
}

// THE FIX: an inconclusive ODE whose variables touch the emptying witness must
// appear in the explanation (pre-fix it was dropped → invalid `¬E` → false unsat).
TEST(ExplanationInconclusiveOde, RelevantOdeReachesExplanation) {
  const Variable t{"t"}, x{"x"};
  ContractorStatus cs{BoxOf({t, x})};

  const Formula emptying = (t == 0.0);            // seeds witness = {t}
  RecordEmptyingConstraint(&cs, emptying);

  const Formula ode = (t + x <= 1.0);             // shares witness var t
  cs.AddInconclusiveOde(ode);

  const std::set<Formula> e = cs.Explanation();
  EXPECT_EQ(e.count(emptying), 1u);
  EXPECT_EQ(e.count(ode), 1u) << "the responsible inconclusive ODE must be in `E`";
}

// MINIMALITY: an inconclusive ODE disjoint from the witness must NOT be spliced —
// otherwise every lemma collects unrelated ODEs.
TEST(ExplanationInconclusiveOde, IrrelevantOdeExcluded) {
  const Variable t{"t"}, z{"z"};
  ContractorStatus cs{BoxOf({t, z})};

  const Formula emptying = (t == 0.0);            // witness = {t}
  RecordEmptyingConstraint(&cs, emptying);

  const Formula ode_disjoint = (z == 3.0);        // no witness var
  cs.AddInconclusiveOde(ode_disjoint);

  EXPECT_EQ(cs.Explanation().count(ode_disjoint), 0u);
}

// THE WITNESS-vs-CLOSURE DECISION: an inconclusive ODE that touches a variable
// reachable only through the relational connectivity closure (`seen`), but not the
// emptying witness itself, must be EXCLUDED. Keying on `seen` would include it (and
// is what dragged the densely-chained BMC ODE web into lemmas → the timeout
// regression); keying on the witness — as the fix does — excludes it.
TEST(ExplanationInconclusiveOde, SplicedOnWitnessNotClosure) {
  const Variable t{"t"}, u{"u"};
  ContractorStatus cs{BoxOf({t, u})};

  // A linking constraint recorded while the box is non-empty: it enters
  // `used_constraints_` without seeding the witness, but shares `t` with the
  // witness so the closure pulls it in and `u` lands in `seen`.
  const Formula linking = (t + u == 0.0);
  cs.AddUsedConstraint(linking);

  const Formula emptying = (t == 0.0);            // witness = {t} (u only in seen)
  RecordEmptyingConstraint(&cs, emptying);

  const Formula ode_closure_only = (u == 3.0);    // touches seen (u) but not witness
  cs.AddInconclusiveOde(ode_closure_only);

  const std::set<Formula> e = cs.Explanation();
  EXPECT_EQ(e.count(linking), 1u);                // closure still works for relational
  EXPECT_EQ(e.count(ode_closure_only), 0u)
      << "ODE must be keyed on the witness, not the broad closure `seen`";
}

// ── INTEGRATION — the real CAPD-divergence path, end-to-end (inline, ~3 s) ───────

// Exception-safe stdout capture (the solver prints its verdict to std::cout).
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

// Solve an inline SMT2 string at the given constraint order (precision 0.001, where
// the flip reproduces). Parsed from the string — no external file dependency.
std::string SolveString(const std::string& smt2, ConstraintOrder order) {
  Config config;
  config.mutable_precision() = 0.001;
  config.mutable_constraint_order() = order;
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  {
    const CoutRedirect redirect{captured.rdbuf()};
    driver.parse_string(smt2);
  }
  return captured.str();
}

bool HasDeltaSat(const std::string& out) {
  return out.find("delta-sat") != std::string::npos;
}
// "unsat" as its own verdict — "delta-sat" contains "sat" but not "unsat".
bool HasUnsat(const std::string& out) {
  return out.find("unsat") != std::string::npos &&
         out.find("delta-sat") == std::string::npos;
}

// A 5-step BMC of the C2E2 inverter hybrid automaton (mode disjunction + per-mode
// `forall_t` invariants + a `timestep = 10000*time` clock-boundary gadget), with the
// flows reduced to a STIFF scalar ODE `v' = 1e6*v^2` — stiff enough that CAPD's
// integrator diverges (found=false) on essentially every box, exactly as the real
// inverter does. That pervasive divergence is what drops the responsible `integral`
// from `used_constraints_` (pre-fix), so `--constraint-order desc` learned a clause
// over the satisfiable relational remainder and returned a false `unsat`; `none`
// returns the witnessed delta-sat. The fix splices the diverged integral back in, so
// all orders agree. RED on the pre-fix solver.
//
// NOTE: this is the smallest faithful end-to-end reproducer found, and the reason it
// can't shrink is now pinned (docs §10). The harvested poisoning lemma's `E` is exactly
// the per-step forced clock-period skeleton (timestep reset + `timestep_t = 50000*time`
// + guard `timestep_t >= 1` + continuity chaining) — NO mode/`v`/integral literal. That
// skeleton holds in the real model, so negating it (after the diverging integral was
// dropped) excludes the model → false `unsat`. Reproducing it needs FOUR co-occurring
// pieces: an entailed relational skeleton, a STATE-dependent stiff divergence (`v'=1e6 v^2`
// integrates fine for small `v`, diverges only near blow-up), mode alternatives so a model
// exists off the conflict, and a search DRIVEN into the diverging conflict the model never
// needs. Ablation (zero the clock / the stiffness / the forall_t) and from-scratch 2-mode
// toys each break one piece and stop flipping. The splice logic itself is covered minimally
// by the unit tests above.
constexpr const char* kC2E2Stiff = R"SMT(
(set-logic QF_NRA_ODE)
(declare-fun v () Real [-100.000000, 100.000000])
(declare-fun t () Real [-100.000000, 100.000000])
(declare-fun stim () Real [-100.000000, 100.000000])
(declare-fun c2e2_drh_timestep () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_telapsed () Real [0.000000, 0.000520])
(declare-fun v_0_0 () Real [-100.000000, 100.000000])
(declare-fun v_0_t () Real [-100.000000, 100.000000])
(declare-fun v_1_0 () Real [-100.000000, 100.000000])
(declare-fun v_1_t () Real [-100.000000, 100.000000])
(declare-fun v_2_0 () Real [-100.000000, 100.000000])
(declare-fun v_2_t () Real [-100.000000, 100.000000])
(declare-fun v_3_0 () Real [-100.000000, 100.000000])
(declare-fun v_3_t () Real [-100.000000, 100.000000])
(declare-fun v_4_0 () Real [-100.000000, 100.000000])
(declare-fun v_4_t () Real [-100.000000, 100.000000])
(declare-fun v_5_0 () Real [-100.000000, 100.000000])
(declare-fun v_5_t () Real [-100.000000, 100.000000])
(declare-fun t_0_0 () Real [-100.000000, 100.000000])
(declare-fun t_0_t () Real [-100.000000, 100.000000])
(declare-fun t_1_0 () Real [-100.000000, 100.000000])
(declare-fun t_1_t () Real [-100.000000, 100.000000])
(declare-fun t_2_0 () Real [-100.000000, 100.000000])
(declare-fun t_2_t () Real [-100.000000, 100.000000])
(declare-fun t_3_0 () Real [-100.000000, 100.000000])
(declare-fun t_3_t () Real [-100.000000, 100.000000])
(declare-fun t_4_0 () Real [-100.000000, 100.000000])
(declare-fun t_4_t () Real [-100.000000, 100.000000])
(declare-fun t_5_0 () Real [-100.000000, 100.000000])
(declare-fun t_5_t () Real [-100.000000, 100.000000])
(declare-fun stim_0_0 () Real [-100.000000, 100.000000])
(declare-fun stim_0_t () Real [-100.000000, 100.000000])
(declare-fun stim_1_0 () Real [-100.000000, 100.000000])
(declare-fun stim_1_t () Real [-100.000000, 100.000000])
(declare-fun stim_2_0 () Real [-100.000000, 100.000000])
(declare-fun stim_2_t () Real [-100.000000, 100.000000])
(declare-fun stim_3_0 () Real [-100.000000, 100.000000])
(declare-fun stim_3_t () Real [-100.000000, 100.000000])
(declare-fun stim_4_0 () Real [-100.000000, 100.000000])
(declare-fun stim_4_t () Real [-100.000000, 100.000000])
(declare-fun stim_5_0 () Real [-100.000000, 100.000000])
(declare-fun stim_5_t () Real [-100.000000, 100.000000])
(declare-fun c2e2_drh_timestep_0_0 () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_0_t () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_1_0 () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_1_t () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_2_0 () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_2_t () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_3_0 () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_3_t () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_4_0 () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_4_t () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_5_0 () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_timestep_5_t () Real [0.000000, 1.000010])
(declare-fun c2e2_drh_telapsed_0_0 () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_0_t () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_1_0 () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_1_t () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_2_0 () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_2_t () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_3_0 () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_3_t () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_4_0 () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_4_t () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_5_0 () Real [0.000000, 0.000520])
(declare-fun c2e2_drh_telapsed_5_t () Real [0.000000, 0.000520])
(declare-fun time_0 () Real [0.000000, 0.000101])
(declare-fun time_1 () Real [0.000000, 0.000101])
(declare-fun time_2 () Real [0.000000, 0.000101])
(declare-fun time_3 () Real [0.000000, 0.000101])
(declare-fun time_4 () Real [0.000000, 0.000101])
(declare-fun time_5 () Real [0.000000, 0.000101])
(declare-fun mode_0 () Real [1.000000, 5.000000])
(declare-fun mode_1 () Real [1.000000, 5.000000])
(declare-fun mode_2 () Real [1.000000, 5.000000])
(declare-fun mode_3 () Real [1.000000, 5.000000])
(declare-fun mode_4 () Real [1.000000, 5.000000])
(declare-fun mode_5 () Real [1.000000, 5.000000])
(define-ode flow_1 ((= d/dt[c2e2_drh_telapsed] 1) (= d/dt[c2e2_drh_timestep] 10000) (= d/dt[stim] 1) (= d/dt[t] 1) (= d/dt[v] (* 1000000. (* v v)))))
(define-ode flow_2 ((= d/dt[c2e2_drh_telapsed] 1) (= d/dt[c2e2_drh_timestep] 10000) (= d/dt[stim] 0) (= d/dt[t] 1) (= d/dt[v] (* 1000000. (* v v)))))
(define-ode flow_3 ((= d/dt[c2e2_drh_telapsed] 1) (= d/dt[c2e2_drh_timestep] 10000) (= d/dt[stim] -1) (= d/dt[t] 1) (= d/dt[v] (* 1000000. (* v v)))))
(define-ode flow_4 ((= d/dt[c2e2_drh_telapsed] 1) (= d/dt[c2e2_drh_timestep] 10000) (= d/dt[stim] 0) (= d/dt[t] 1) (= d/dt[v] (* 1000000. (* v v)))))
(define-ode flow_5 ((= d/dt[c2e2_drh_telapsed] 1) (= d/dt[c2e2_drh_timestep] 0) (= d/dt[stim] 0) (= d/dt[t] 0) (= d/dt[v] 0)))
(assert (and (= mode_5 1) (or (and (= v_5_t (+ v_5_0 (* 0 time_5))) (= t_5_t (+ t_5_0 (* 0 time_5))) (= stim_5_t (+ stim_5_0 (* 0 time_5))) (= c2e2_drh_timestep_5_t (+ c2e2_drh_timestep_5_0 (* 0 time_5))) (= c2e2_drh_telapsed_5_t (+ c2e2_drh_telapsed_5_0 (* 1 time_5))) (= [c2e2_drh_telapsed_5_t c2e2_drh_timestep_5_t stim_5_t t_5_t v_5_t] (integral 0. time_5 [c2e2_drh_telapsed_5_0 c2e2_drh_timestep_5_0 stim_5_0 t_5_0 v_5_0] flow_5)) (= mode_5 5)) (and (= t_5_t (+ t_5_0 (* 1 time_5))) (= stim_5_t (+ stim_5_0 (* 0 time_5))) (= c2e2_drh_timestep_5_t (+ c2e2_drh_timestep_5_0 (* 10000 time_5))) (= c2e2_drh_telapsed_5_t (+ c2e2_drh_telapsed_5_0 (* 1 time_5))) (= [c2e2_drh_telapsed_5_t c2e2_drh_timestep_5_t stim_5_t t_5_t v_5_t] (integral 0. time_5 [c2e2_drh_telapsed_5_0 c2e2_drh_timestep_5_0 stim_5_0 t_5_0 v_5_0] flow_4)) (= mode_5 4) (forall_t 4 [0 time_5] (< t_5_t 2)) (< t_5_t 2) (< t_5_0 2)) (and (= t_5_t (+ t_5_0 (* 1 time_5))) (= stim_5_t (+ stim_5_0 (* -1 time_5))) (= c2e2_drh_timestep_5_t (+ c2e2_drh_timestep_5_0 (* 10000 time_5))) (= c2e2_drh_telapsed_5_t (+ c2e2_drh_telapsed_5_0 (* 1 time_5))) (= [c2e2_drh_telapsed_5_t c2e2_drh_timestep_5_t stim_5_t t_5_t v_5_t] (integral 0. time_5 [c2e2_drh_telapsed_5_0 c2e2_drh_timestep_5_0 stim_5_0 t_5_0 v_5_0] flow_3)) (= mode_5 3) (forall_t 3 [0 time_5] (> stim_5_t 0)) (> stim_5_t 0) (> stim_5_0 0)) (and (= t_5_t (+ t_5_0 (* 1 time_5))) (= stim_5_t (+ stim_5_0 (* 0 time_5))) (= c2e2_drh_timestep_5_t (+ c2e2_drh_timestep_5_0 (* 10000 time_5))) (= c2e2_drh_telapsed_5_t (+ c2e2_drh_telapsed_5_0 (* 1 time_5))) (= [c2e2_drh_telapsed_5_t c2e2_drh_timestep_5_t stim_5_t t_5_t v_5_t] (integral 0. time_5 [c2e2_drh_telapsed_5_0 c2e2_drh_timestep_5_0 stim_5_0 t_5_0 v_5_0] flow_2)) (= mode_5 2) (forall_t 2 [0 time_5] (< t_5_t 2)) (< t_5_t 2) (< t_5_0 2)) (and (= t_5_t (+ t_5_0 (* 1 time_5))) (= stim_5_t (+ stim_5_0 (* 1 time_5))) (= c2e2_drh_timestep_5_t (+ c2e2_drh_timestep_5_0 (* 10000 time_5))) (= c2e2_drh_telapsed_5_t (+ c2e2_drh_telapsed_5_0 (* 1 time_5))) (= [c2e2_drh_telapsed_5_t c2e2_drh_timestep_5_t stim_5_t t_5_t v_5_t] (integral 0. time_5 [c2e2_drh_telapsed_5_0 c2e2_drh_timestep_5_0 stim_5_0 t_5_0 v_5_0] flow_1)) (= mode_5 1) (forall_t 1 [0 time_5] (< stim_5_t 1.2)) (< stim_5_t 1.2) (< stim_5_0 1.2))) (or (and (= v_4_t (+ v_4_0 (* 0 time_4))) (= t_4_t (+ t_4_0 (* 0 time_4))) (= stim_4_t (+ stim_4_0 (* 0 time_4))) (= c2e2_drh_timestep_4_t (+ c2e2_drh_timestep_4_0 (* 0 time_4))) (= c2e2_drh_telapsed_4_t (+ c2e2_drh_telapsed_4_0 (* 1 time_4))) (= [c2e2_drh_telapsed_4_t c2e2_drh_timestep_4_t stim_4_t t_4_t v_4_t] (integral 0. time_4 [c2e2_drh_telapsed_4_0 c2e2_drh_timestep_4_0 stim_4_0 t_4_0 v_4_0] flow_5)) (= mode_4 5) (= mode_5 5) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0)) (and (or (and (= mode_5 1) (>= t_4_t 2) (= stim_5_0 0) (= t_5_0 0) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= v_4_t v_5_0)) (and (= mode_5 5) (> v_4_t 1.3200000000000001) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0)) (and (= mode_5 1) (= c2e2_drh_timestep_5_0 0) (>= c2e2_drh_timestep_4_t 1) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0))) (= t_4_t (+ t_4_0 (* 1 time_4))) (= stim_4_t (+ stim_4_0 (* 0 time_4))) (= c2e2_drh_timestep_4_t (+ c2e2_drh_timestep_4_0 (* 10000 time_4))) (= c2e2_drh_telapsed_4_t (+ c2e2_drh_telapsed_4_0 (* 1 time_4))) (= [c2e2_drh_telapsed_4_t c2e2_drh_timestep_4_t stim_4_t t_4_t v_4_t] (integral 0. time_4 [c2e2_drh_telapsed_4_0 c2e2_drh_timestep_4_0 stim_4_0 t_4_0 v_4_0] flow_4)) (= mode_4 4) (forall_t 4 [0 time_4] (< t_4_t 2)) (< t_4_t 2) (< t_4_0 2)) (and (or (and (= mode_5 4) (<= stim_4_t 0) (= stim_5_0 0) (= t_5_0 0) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= v_4_t v_5_0)) (and (= mode_5 5) (> v_4_t 1.3200000000000001) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0)) (and (= mode_5 1) (= c2e2_drh_timestep_5_0 0) (>= c2e2_drh_timestep_4_t 1) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0))) (= t_4_t (+ t_4_0 (* 1 time_4))) (= stim_4_t (+ stim_4_0 (* -1 time_4))) (= c2e2_drh_timestep_4_t (+ c2e2_drh_timestep_4_0 (* 10000 time_4))) (= c2e2_drh_telapsed_4_t (+ c2e2_drh_telapsed_4_0 (* 1 time_4))) (= [c2e2_drh_telapsed_4_t c2e2_drh_timestep_4_t stim_4_t t_4_t v_4_t] (integral 0. time_4 [c2e2_drh_telapsed_4_0 c2e2_drh_timestep_4_0 stim_4_0 t_4_0 v_4_0] flow_3)) (= mode_4 3) (forall_t 3 [0 time_4] (> stim_4_t 0)) (> stim_4_t 0) (> stim_4_0 0)) (and (or (and (= mode_5 3) (>= t_4_t 2) (= stim_5_0 1.2) (= t_5_0 0) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= v_4_t v_5_0)) (and (= mode_5 5) (> v_4_t 1.3200000000000001) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0)) (and (= mode_5 1) (= c2e2_drh_timestep_5_0 0) (>= c2e2_drh_timestep_4_t 1) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0))) (= t_4_t (+ t_4_0 (* 1 time_4))) (= stim_4_t (+ stim_4_0 (* 0 time_4))) (= c2e2_drh_timestep_4_t (+ c2e2_drh_timestep_4_0 (* 10000 time_4))) (= c2e2_drh_telapsed_4_t (+ c2e2_drh_telapsed_4_0 (* 1 time_4))) (= [c2e2_drh_telapsed_4_t c2e2_drh_timestep_4_t stim_4_t t_4_t v_4_t] (integral 0. time_4 [c2e2_drh_telapsed_4_0 c2e2_drh_timestep_4_0 stim_4_0 t_4_0 v_4_0] flow_2)) (= mode_4 2) (forall_t 2 [0 time_4] (< t_4_t 2)) (< t_4_t 2) (< t_4_0 2)) (and (or (and (= mode_5 2) (>= stim_4_t 1.2) (= stim_5_0 1.2) (= t_5_0 0) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= v_4_t v_5_0)) (and (= mode_5 5) (> v_4_t 1.3200000000000001) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= c2e2_drh_timestep_4_t c2e2_drh_timestep_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0)) (and (= mode_5 1) (= c2e2_drh_timestep_5_0 0) (>= c2e2_drh_timestep_4_t 1) (= c2e2_drh_telapsed_4_t c2e2_drh_telapsed_5_0) (= stim_4_t stim_5_0) (= t_4_t t_5_0) (= v_4_t v_5_0))) (= t_4_t (+ t_4_0 (* 1 time_4))) (= stim_4_t (+ stim_4_0 (* 1 time_4))) (= c2e2_drh_timestep_4_t (+ c2e2_drh_timestep_4_0 (* 10000 time_4))) (= c2e2_drh_telapsed_4_t (+ c2e2_drh_telapsed_4_0 (* 1 time_4))) (= [c2e2_drh_telapsed_4_t c2e2_drh_timestep_4_t stim_4_t t_4_t v_4_t] (integral 0. time_4 [c2e2_drh_telapsed_4_0 c2e2_drh_timestep_4_0 stim_4_0 t_4_0 v_4_0] flow_1)) (= mode_4 1) (forall_t 1 [0 time_4] (< stim_4_t 1.2)) (< stim_4_t 1.2) (< stim_4_0 1.2))) (or (and (= v_3_t (+ v_3_0 (* 0 time_3))) (= t_3_t (+ t_3_0 (* 0 time_3))) (= stim_3_t (+ stim_3_0 (* 0 time_3))) (= c2e2_drh_timestep_3_t (+ c2e2_drh_timestep_3_0 (* 0 time_3))) (= c2e2_drh_telapsed_3_t (+ c2e2_drh_telapsed_3_0 (* 1 time_3))) (= [c2e2_drh_telapsed_3_t c2e2_drh_timestep_3_t stim_3_t t_3_t v_3_t] (integral 0. time_3 [c2e2_drh_telapsed_3_0 c2e2_drh_timestep_3_0 stim_3_0 t_3_0 v_3_0] flow_5)) (= mode_3 5) (= mode_4 5) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0)) (and (or (and (= mode_4 1) (>= t_3_t 2) (= stim_4_0 0) (= t_4_0 0) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= v_3_t v_4_0)) (and (= mode_4 5) (> v_3_t 1.3200000000000001) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0)) (and (= mode_4 1) (= c2e2_drh_timestep_4_0 0) (>= c2e2_drh_timestep_3_t 1) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0))) (= t_3_t (+ t_3_0 (* 1 time_3))) (= stim_3_t (+ stim_3_0 (* 0 time_3))) (= c2e2_drh_timestep_3_t (+ c2e2_drh_timestep_3_0 (* 10000 time_3))) (= c2e2_drh_telapsed_3_t (+ c2e2_drh_telapsed_3_0 (* 1 time_3))) (= [c2e2_drh_telapsed_3_t c2e2_drh_timestep_3_t stim_3_t t_3_t v_3_t] (integral 0. time_3 [c2e2_drh_telapsed_3_0 c2e2_drh_timestep_3_0 stim_3_0 t_3_0 v_3_0] flow_4)) (= mode_3 4) (forall_t 4 [0 time_3] (< t_3_t 2)) (< t_3_t 2) (< t_3_0 2)) (and (or (and (= mode_4 4) (<= stim_3_t 0) (= stim_4_0 0) (= t_4_0 0) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= v_3_t v_4_0)) (and (= mode_4 5) (> v_3_t 1.3200000000000001) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0)) (and (= mode_4 1) (= c2e2_drh_timestep_4_0 0) (>= c2e2_drh_timestep_3_t 1) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0))) (= t_3_t (+ t_3_0 (* 1 time_3))) (= stim_3_t (+ stim_3_0 (* -1 time_3))) (= c2e2_drh_timestep_3_t (+ c2e2_drh_timestep_3_0 (* 10000 time_3))) (= c2e2_drh_telapsed_3_t (+ c2e2_drh_telapsed_3_0 (* 1 time_3))) (= [c2e2_drh_telapsed_3_t c2e2_drh_timestep_3_t stim_3_t t_3_t v_3_t] (integral 0. time_3 [c2e2_drh_telapsed_3_0 c2e2_drh_timestep_3_0 stim_3_0 t_3_0 v_3_0] flow_3)) (= mode_3 3) (forall_t 3 [0 time_3] (> stim_3_t 0)) (> stim_3_t 0) (> stim_3_0 0)) (and (or (and (= mode_4 3) (>= t_3_t 2) (= stim_4_0 1.2) (= t_4_0 0) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= v_3_t v_4_0)) (and (= mode_4 5) (> v_3_t 1.3200000000000001) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0)) (and (= mode_4 1) (= c2e2_drh_timestep_4_0 0) (>= c2e2_drh_timestep_3_t 1) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0))) (= t_3_t (+ t_3_0 (* 1 time_3))) (= stim_3_t (+ stim_3_0 (* 0 time_3))) (= c2e2_drh_timestep_3_t (+ c2e2_drh_timestep_3_0 (* 10000 time_3))) (= c2e2_drh_telapsed_3_t (+ c2e2_drh_telapsed_3_0 (* 1 time_3))) (= [c2e2_drh_telapsed_3_t c2e2_drh_timestep_3_t stim_3_t t_3_t v_3_t] (integral 0. time_3 [c2e2_drh_telapsed_3_0 c2e2_drh_timestep_3_0 stim_3_0 t_3_0 v_3_0] flow_2)) (= mode_3 2) (forall_t 2 [0 time_3] (< t_3_t 2)) (< t_3_t 2) (< t_3_0 2)) (and (or (and (= mode_4 2) (>= stim_3_t 1.2) (= stim_4_0 1.2) (= t_4_0 0) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= v_3_t v_4_0)) (and (= mode_4 5) (> v_3_t 1.3200000000000001) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= c2e2_drh_timestep_3_t c2e2_drh_timestep_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0)) (and (= mode_4 1) (= c2e2_drh_timestep_4_0 0) (>= c2e2_drh_timestep_3_t 1) (= c2e2_drh_telapsed_3_t c2e2_drh_telapsed_4_0) (= stim_3_t stim_4_0) (= t_3_t t_4_0) (= v_3_t v_4_0))) (= t_3_t (+ t_3_0 (* 1 time_3))) (= stim_3_t (+ stim_3_0 (* 1 time_3))) (= c2e2_drh_timestep_3_t (+ c2e2_drh_timestep_3_0 (* 10000 time_3))) (= c2e2_drh_telapsed_3_t (+ c2e2_drh_telapsed_3_0 (* 1 time_3))) (= [c2e2_drh_telapsed_3_t c2e2_drh_timestep_3_t stim_3_t t_3_t v_3_t] (integral 0. time_3 [c2e2_drh_telapsed_3_0 c2e2_drh_timestep_3_0 stim_3_0 t_3_0 v_3_0] flow_1)) (= mode_3 1) (forall_t 1 [0 time_3] (< stim_3_t 1.2)) (< stim_3_t 1.2) (< stim_3_0 1.2))) (or (and (= v_2_t (+ v_2_0 (* 0 time_2))) (= t_2_t (+ t_2_0 (* 0 time_2))) (= stim_2_t (+ stim_2_0 (* 0 time_2))) (= c2e2_drh_timestep_2_t (+ c2e2_drh_timestep_2_0 (* 0 time_2))) (= c2e2_drh_telapsed_2_t (+ c2e2_drh_telapsed_2_0 (* 1 time_2))) (= [c2e2_drh_telapsed_2_t c2e2_drh_timestep_2_t stim_2_t t_2_t v_2_t] (integral 0. time_2 [c2e2_drh_telapsed_2_0 c2e2_drh_timestep_2_0 stim_2_0 t_2_0 v_2_0] flow_5)) (= mode_2 5) (= mode_3 5) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0)) (and (or (and (= mode_3 1) (>= t_2_t 2) (= stim_3_0 0) (= t_3_0 0) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= v_2_t v_3_0)) (and (= mode_3 5) (> v_2_t 1.3200000000000001) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0)) (and (= mode_3 1) (= c2e2_drh_timestep_3_0 0) (>= c2e2_drh_timestep_2_t 1) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0))) (= t_2_t (+ t_2_0 (* 1 time_2))) (= stim_2_t (+ stim_2_0 (* 0 time_2))) (= c2e2_drh_timestep_2_t (+ c2e2_drh_timestep_2_0 (* 10000 time_2))) (= c2e2_drh_telapsed_2_t (+ c2e2_drh_telapsed_2_0 (* 1 time_2))) (= [c2e2_drh_telapsed_2_t c2e2_drh_timestep_2_t stim_2_t t_2_t v_2_t] (integral 0. time_2 [c2e2_drh_telapsed_2_0 c2e2_drh_timestep_2_0 stim_2_0 t_2_0 v_2_0] flow_4)) (= mode_2 4) (forall_t 4 [0 time_2] (< t_2_t 2)) (< t_2_t 2) (< t_2_0 2)) (and (or (and (= mode_3 4) (<= stim_2_t 0) (= stim_3_0 0) (= t_3_0 0) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= v_2_t v_3_0)) (and (= mode_3 5) (> v_2_t 1.3200000000000001) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0)) (and (= mode_3 1) (= c2e2_drh_timestep_3_0 0) (>= c2e2_drh_timestep_2_t 1) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0))) (= t_2_t (+ t_2_0 (* 1 time_2))) (= stim_2_t (+ stim_2_0 (* -1 time_2))) (= c2e2_drh_timestep_2_t (+ c2e2_drh_timestep_2_0 (* 10000 time_2))) (= c2e2_drh_telapsed_2_t (+ c2e2_drh_telapsed_2_0 (* 1 time_2))) (= [c2e2_drh_telapsed_2_t c2e2_drh_timestep_2_t stim_2_t t_2_t v_2_t] (integral 0. time_2 [c2e2_drh_telapsed_2_0 c2e2_drh_timestep_2_0 stim_2_0 t_2_0 v_2_0] flow_3)) (= mode_2 3) (forall_t 3 [0 time_2] (> stim_2_t 0)) (> stim_2_t 0) (> stim_2_0 0)) (and (or (and (= mode_3 3) (>= t_2_t 2) (= stim_3_0 1.2) (= t_3_0 0) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= v_2_t v_3_0)) (and (= mode_3 5) (> v_2_t 1.3200000000000001) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0)) (and (= mode_3 1) (= c2e2_drh_timestep_3_0 0) (>= c2e2_drh_timestep_2_t 1) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0))) (= t_2_t (+ t_2_0 (* 1 time_2))) (= stim_2_t (+ stim_2_0 (* 0 time_2))) (= c2e2_drh_timestep_2_t (+ c2e2_drh_timestep_2_0 (* 10000 time_2))) (= c2e2_drh_telapsed_2_t (+ c2e2_drh_telapsed_2_0 (* 1 time_2))) (= [c2e2_drh_telapsed_2_t c2e2_drh_timestep_2_t stim_2_t t_2_t v_2_t] (integral 0. time_2 [c2e2_drh_telapsed_2_0 c2e2_drh_timestep_2_0 stim_2_0 t_2_0 v_2_0] flow_2)) (= mode_2 2) (forall_t 2 [0 time_2] (< t_2_t 2)) (< t_2_t 2) (< t_2_0 2)) (and (or (and (= mode_3 2) (>= stim_2_t 1.2) (= stim_3_0 1.2) (= t_3_0 0) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= v_2_t v_3_0)) (and (= mode_3 5) (> v_2_t 1.3200000000000001) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= c2e2_drh_timestep_2_t c2e2_drh_timestep_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0)) (and (= mode_3 1) (= c2e2_drh_timestep_3_0 0) (>= c2e2_drh_timestep_2_t 1) (= c2e2_drh_telapsed_2_t c2e2_drh_telapsed_3_0) (= stim_2_t stim_3_0) (= t_2_t t_3_0) (= v_2_t v_3_0))) (= t_2_t (+ t_2_0 (* 1 time_2))) (= stim_2_t (+ stim_2_0 (* 1 time_2))) (= c2e2_drh_timestep_2_t (+ c2e2_drh_timestep_2_0 (* 10000 time_2))) (= c2e2_drh_telapsed_2_t (+ c2e2_drh_telapsed_2_0 (* 1 time_2))) (= [c2e2_drh_telapsed_2_t c2e2_drh_timestep_2_t stim_2_t t_2_t v_2_t] (integral 0. time_2 [c2e2_drh_telapsed_2_0 c2e2_drh_timestep_2_0 stim_2_0 t_2_0 v_2_0] flow_1)) (= mode_2 1) (forall_t 1 [0 time_2] (< stim_2_t 1.2)) (< stim_2_t 1.2) (< stim_2_0 1.2))) (or (and (= v_1_t (+ v_1_0 (* 0 time_1))) (= t_1_t (+ t_1_0 (* 0 time_1))) (= stim_1_t (+ stim_1_0 (* 0 time_1))) (= c2e2_drh_timestep_1_t (+ c2e2_drh_timestep_1_0 (* 0 time_1))) (= c2e2_drh_telapsed_1_t (+ c2e2_drh_telapsed_1_0 (* 1 time_1))) (= [c2e2_drh_telapsed_1_t c2e2_drh_timestep_1_t stim_1_t t_1_t v_1_t] (integral 0. time_1 [c2e2_drh_telapsed_1_0 c2e2_drh_timestep_1_0 stim_1_0 t_1_0 v_1_0] flow_5)) (= mode_1 5) (= mode_2 5) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0)) (and (or (and (= mode_2 1) (>= t_1_t 2) (= stim_2_0 0) (= t_2_0 0) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= v_1_t v_2_0)) (and (= mode_2 5) (> v_1_t 1.3200000000000001) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0)) (and (= mode_2 1) (= c2e2_drh_timestep_2_0 0) (>= c2e2_drh_timestep_1_t 1) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0))) (= t_1_t (+ t_1_0 (* 1 time_1))) (= stim_1_t (+ stim_1_0 (* 0 time_1))) (= c2e2_drh_timestep_1_t (+ c2e2_drh_timestep_1_0 (* 10000 time_1))) (= c2e2_drh_telapsed_1_t (+ c2e2_drh_telapsed_1_0 (* 1 time_1))) (= [c2e2_drh_telapsed_1_t c2e2_drh_timestep_1_t stim_1_t t_1_t v_1_t] (integral 0. time_1 [c2e2_drh_telapsed_1_0 c2e2_drh_timestep_1_0 stim_1_0 t_1_0 v_1_0] flow_4)) (= mode_1 4) (forall_t 4 [0 time_1] (< t_1_t 2)) (< t_1_t 2) (< t_1_0 2)) (and (or (and (= mode_2 4) (<= stim_1_t 0) (= stim_2_0 0) (= t_2_0 0) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= v_1_t v_2_0)) (and (= mode_2 5) (> v_1_t 1.3200000000000001) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0)) (and (= mode_2 1) (= c2e2_drh_timestep_2_0 0) (>= c2e2_drh_timestep_1_t 1) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0))) (= t_1_t (+ t_1_0 (* 1 time_1))) (= stim_1_t (+ stim_1_0 (* -1 time_1))) (= c2e2_drh_timestep_1_t (+ c2e2_drh_timestep_1_0 (* 10000 time_1))) (= c2e2_drh_telapsed_1_t (+ c2e2_drh_telapsed_1_0 (* 1 time_1))) (= [c2e2_drh_telapsed_1_t c2e2_drh_timestep_1_t stim_1_t t_1_t v_1_t] (integral 0. time_1 [c2e2_drh_telapsed_1_0 c2e2_drh_timestep_1_0 stim_1_0 t_1_0 v_1_0] flow_3)) (= mode_1 3) (forall_t 3 [0 time_1] (> stim_1_t 0)) (> stim_1_t 0) (> stim_1_0 0)) (and (or (and (= mode_2 3) (>= t_1_t 2) (= stim_2_0 1.2) (= t_2_0 0) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= v_1_t v_2_0)) (and (= mode_2 5) (> v_1_t 1.3200000000000001) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0)) (and (= mode_2 1) (= c2e2_drh_timestep_2_0 0) (>= c2e2_drh_timestep_1_t 1) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0))) (= t_1_t (+ t_1_0 (* 1 time_1))) (= stim_1_t (+ stim_1_0 (* 0 time_1))) (= c2e2_drh_timestep_1_t (+ c2e2_drh_timestep_1_0 (* 10000 time_1))) (= c2e2_drh_telapsed_1_t (+ c2e2_drh_telapsed_1_0 (* 1 time_1))) (= [c2e2_drh_telapsed_1_t c2e2_drh_timestep_1_t stim_1_t t_1_t v_1_t] (integral 0. time_1 [c2e2_drh_telapsed_1_0 c2e2_drh_timestep_1_0 stim_1_0 t_1_0 v_1_0] flow_2)) (= mode_1 2) (forall_t 2 [0 time_1] (< t_1_t 2)) (< t_1_t 2) (< t_1_0 2)) (and (or (and (= mode_2 2) (>= stim_1_t 1.2) (= stim_2_0 1.2) (= t_2_0 0) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= v_1_t v_2_0)) (and (= mode_2 5) (> v_1_t 1.3200000000000001) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= c2e2_drh_timestep_1_t c2e2_drh_timestep_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0)) (and (= mode_2 1) (= c2e2_drh_timestep_2_0 0) (>= c2e2_drh_timestep_1_t 1) (= c2e2_drh_telapsed_1_t c2e2_drh_telapsed_2_0) (= stim_1_t stim_2_0) (= t_1_t t_2_0) (= v_1_t v_2_0))) (= t_1_t (+ t_1_0 (* 1 time_1))) (= stim_1_t (+ stim_1_0 (* 1 time_1))) (= c2e2_drh_timestep_1_t (+ c2e2_drh_timestep_1_0 (* 10000 time_1))) (= c2e2_drh_telapsed_1_t (+ c2e2_drh_telapsed_1_0 (* 1 time_1))) (= [c2e2_drh_telapsed_1_t c2e2_drh_timestep_1_t stim_1_t t_1_t v_1_t] (integral 0. time_1 [c2e2_drh_telapsed_1_0 c2e2_drh_timestep_1_0 stim_1_0 t_1_0 v_1_0] flow_1)) (= mode_1 1) (forall_t 1 [0 time_1] (< stim_1_t 1.2)) (< stim_1_t 1.2) (< stim_1_0 1.2))) (or (and (= v_0_t (+ v_0_0 (* 0 time_0))) (= t_0_t (+ t_0_0 (* 0 time_0))) (= stim_0_t (+ stim_0_0 (* 0 time_0))) (= c2e2_drh_timestep_0_t (+ c2e2_drh_timestep_0_0 (* 0 time_0))) (= c2e2_drh_telapsed_0_t (+ c2e2_drh_telapsed_0_0 (* 1 time_0))) (= [c2e2_drh_telapsed_0_t c2e2_drh_timestep_0_t stim_0_t t_0_t v_0_t] (integral 0. time_0 [c2e2_drh_telapsed_0_0 c2e2_drh_timestep_0_0 stim_0_0 t_0_0 v_0_0] flow_5)) (= mode_0 5) (= mode_1 5) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0)) (and (or (and (= mode_1 1) (>= t_0_t 2) (= stim_1_0 0) (= t_1_0 0) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= v_0_t v_1_0)) (and (= mode_1 5) (> v_0_t 1.3200000000000001) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0)) (and (= mode_1 1) (= c2e2_drh_timestep_1_0 0) (>= c2e2_drh_timestep_0_t 1) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0))) (= t_0_t (+ t_0_0 (* 1 time_0))) (= stim_0_t (+ stim_0_0 (* 0 time_0))) (= c2e2_drh_timestep_0_t (+ c2e2_drh_timestep_0_0 (* 10000 time_0))) (= c2e2_drh_telapsed_0_t (+ c2e2_drh_telapsed_0_0 (* 1 time_0))) (= [c2e2_drh_telapsed_0_t c2e2_drh_timestep_0_t stim_0_t t_0_t v_0_t] (integral 0. time_0 [c2e2_drh_telapsed_0_0 c2e2_drh_timestep_0_0 stim_0_0 t_0_0 v_0_0] flow_4)) (= mode_0 4) (forall_t 4 [0 time_0] (< t_0_t 2)) (< t_0_t 2) (< t_0_0 2)) (and (or (and (= mode_1 4) (<= stim_0_t 0) (= stim_1_0 0) (= t_1_0 0) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= v_0_t v_1_0)) (and (= mode_1 5) (> v_0_t 1.3200000000000001) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0)) (and (= mode_1 1) (= c2e2_drh_timestep_1_0 0) (>= c2e2_drh_timestep_0_t 1) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0))) (= t_0_t (+ t_0_0 (* 1 time_0))) (= stim_0_t (+ stim_0_0 (* -1 time_0))) (= c2e2_drh_timestep_0_t (+ c2e2_drh_timestep_0_0 (* 10000 time_0))) (= c2e2_drh_telapsed_0_t (+ c2e2_drh_telapsed_0_0 (* 1 time_0))) (= [c2e2_drh_telapsed_0_t c2e2_drh_timestep_0_t stim_0_t t_0_t v_0_t] (integral 0. time_0 [c2e2_drh_telapsed_0_0 c2e2_drh_timestep_0_0 stim_0_0 t_0_0 v_0_0] flow_3)) (= mode_0 3) (forall_t 3 [0 time_0] (> stim_0_t 0)) (> stim_0_t 0) (> stim_0_0 0)) (and (or (and (= mode_1 3) (>= t_0_t 2) (= stim_1_0 1.2) (= t_1_0 0) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= v_0_t v_1_0)) (and (= mode_1 5) (> v_0_t 1.3200000000000001) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0)) (and (= mode_1 1) (= c2e2_drh_timestep_1_0 0) (>= c2e2_drh_timestep_0_t 1) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0))) (= t_0_t (+ t_0_0 (* 1 time_0))) (= stim_0_t (+ stim_0_0 (* 0 time_0))) (= c2e2_drh_timestep_0_t (+ c2e2_drh_timestep_0_0 (* 10000 time_0))) (= c2e2_drh_telapsed_0_t (+ c2e2_drh_telapsed_0_0 (* 1 time_0))) (= [c2e2_drh_telapsed_0_t c2e2_drh_timestep_0_t stim_0_t t_0_t v_0_t] (integral 0. time_0 [c2e2_drh_telapsed_0_0 c2e2_drh_timestep_0_0 stim_0_0 t_0_0 v_0_0] flow_2)) (= mode_0 2) (forall_t 2 [0 time_0] (< t_0_t 2)) (< t_0_t 2) (< t_0_0 2)) (and (or (and (= mode_1 2) (>= stim_0_t 1.2) (= stim_1_0 1.2) (= t_1_0 0) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= v_0_t v_1_0)) (and (= mode_1 5) (> v_0_t 1.3200000000000001) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= c2e2_drh_timestep_0_t c2e2_drh_timestep_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0)) (and (= mode_1 1) (= c2e2_drh_timestep_1_0 0) (>= c2e2_drh_timestep_0_t 1) (= c2e2_drh_telapsed_0_t c2e2_drh_telapsed_1_0) (= stim_0_t stim_1_0) (= t_0_t t_1_0) (= v_0_t v_1_0))) (= t_0_t (+ t_0_0 (* 1 time_0))) (= stim_0_t (+ stim_0_0 (* 1 time_0))) (= c2e2_drh_timestep_0_t (+ c2e2_drh_timestep_0_0 (* 10000 time_0))) (= c2e2_drh_telapsed_0_t (+ c2e2_drh_telapsed_0_0 (* 1 time_0))) (= [c2e2_drh_telapsed_0_t c2e2_drh_timestep_0_t stim_0_t t_0_t v_0_t] (integral 0. time_0 [c2e2_drh_telapsed_0_0 c2e2_drh_timestep_0_0 stim_0_0 t_0_0 v_0_0] flow_1)) (= mode_0 1) (forall_t 1 [0 time_0] (< stim_0_t 1.2)) (< stim_0_t 1.2) (< stim_0_0 1.2))) (and (= c2e2_drh_telapsed_0_0 0) (= c2e2_drh_timestep_0_0 0) (= t_0_0 0) (<= v_0_0 1.21) (>= v_0_0 1.1899999999999999) (= stim_0_0 0)) (= mode_0 1)))
(check-sat)
(exit)
)SMT";

TEST(ExplanationInconclusiveOde, DivergingOdeNoFalseUnsatEndToEnd) {
  const std::string none{SolveString(kC2E2Stiff, ConstraintOrder::kNone)};
  const std::string desc{SolveString(kC2E2Stiff, ConstraintOrder::kDesc)};
  EXPECT_TRUE(HasDeltaSat(none)) << "instance is genuinely delta-sat";
  EXPECT_TRUE(HasDeltaSat(desc)) << "desc must not flip to a false `unsat`";
  EXPECT_FALSE(HasUnsat(desc));
}

}  // namespace
}  // namespace dreal
