# Design Decisions (ADRs)

Topic-keyed architecture/soundness decision records. Each entry states the decision, the
load-bearing reason that survives even if the decision changes, the alternatives tried and
rejected, and the implementation site. Performance tried/rejected experiments live in
`OPTIMIZATION_LOG.md`; this file is the architectural + soundness record.

---

## ODE backend: CAPD-only (Codac removed)

**Decision:** CAPD master (`CAPD_INTERVAL_TYPE=NATIVE`, order-20 `IOdeSolver` + `ITimeMap`) is
the sole ODE backend, source-built from the IBEX fork (`ncsys-lab/ibex-lib@dreal-perf-patches`)
+ CAPD. Codac and Eigen3 are no longer dependencies.

**Why:** Two prior stacks were tried and discarded. (1) The pre-Codac `ncsys-lab` IBEX/CAPD
fork was an unmaintained x86-only snapshot (CAPD 4.x pulled FILIB, which `FATAL_ERROR`s on
non-x86). (2) Codac v2 + `lebarsfa/ibex-lib` got onto a maintained ARM64 stack but at order-2
Taylor (`CtcLohner`, hardcoded), and introduced a **dominant non-ODE slowdown** that was
root-caused by profiling: ~65% of `1mhz` saradc wall time was `ibex::Function::init` building
the `Gradient` / `ExprLinearity` objects unconditionally on every contractor-cache miss — work
`Function::backward` (the only path HC4 uses) never reads. The fix is the fork's lazy-grad
patch (build `_grad` on first use), which made CAPD-only viable without Codac.

**Alternatives tried and rejected:**
- *Keep Codac as the short-horizon "fast path" (gated hybrid).* Eliminated by direct
  measurement: native ARM64 CAPD order-20 was **never slower** than Codac order-2 across every
  tested benchmark, and ~23% faster on the canonical `bouncing_ball` reference where Codac was
  supposed to win — higher Taylor order needs fewer timesteps, and ARM64 per-step overhead is
  low enough that fewer steps wins. The ODE backend is not the bottleneck anyway (SAT layer +
  IBEX fwdbwd + `forall_t` dominate wall time).
- *Fork `lebarsfa/ibex-lib` and keep Codac.* `lebarsfa` was 100+ commits behind
  `ibex-team/ibex-lib` and not project-controlled; Codac shipped as a per-arch-per-OS prebuilt
  ZIP (a maintenance liability on each new macOS/glibc). Forking `ibex-team` directly and
  dropping Codac removes both — `IBEXConfig.cmake` (lebarsfa's only needed feature) was required
  only by Codac's own build, and dReal wires IBEX via manual IMPORTED targets, never
  `find_package(IBEX)`.

**Code:** `CMakeLists.txt` (`ExternalProject_Add(ibex_external)` + `capd_external`);
`src/dreal/contractor/odes/`. See `DEPENDENCIES.md` for the current build wiring and
`../ibex-fork/MIGRATION.md` for the fork patch catalog.

---

## Per-slice ODE tube, not a coarse endpoint hull

**Decision:** The ODE contractor builds the **time-ordered per-slice tube** (each adaptive
step's Taylor curve sub-gridded into `kHullGrid=16` enclosures) and filters slice-by-slice —
it does not intersect the terminal box with a single endpoint enclosure.

**Why:** The single-endpoint form is **unsound for free-time integrals**. Intersecting only
the `enclosure(t_ub)` over-narrows away a solution reached at an *interior* time `< t_ub`,
producing a false `unsat` — the catastrophic direction for a delta-complete solver. Proven on
`github_oct5_0hz_k2_prostate_cancer_*`: the coarse form returned `unsat` in 0.03 s while the
per-slice form (and cav26) return `delta-sat` with a witness at interior times ≈1.7–4.4 «
horizon 20. A coarse hull also collapses per-time/per-component correlation, under-refuting
anti-correlated tubes and missing interior-only invariant violations.

**Alternatives tried and rejected:** the coarse single-endpoint intersection (the
Codac→CAPD rewrite's form) — unsound as above. dReal3 segfaults on these inputs, so cav26 +
the explicit witness are the oracle.

**Code:** `contractor_ode_lohner::Prune` + `integrate_tube_slices`
(`src/dreal/contractor/odes/`). Mechanism detail: `docs/ode-integration.md` §Mechanism.
Regression coverage: `contractor_odes_semantic_test.cc` (`GravityInvariantTest`,
`AntiCorrelatedTest`, `DecayFlowTest.*`).

---

## ODE feed faithfulness: 17-digit constant rendering

**Decision:** `to_capd_string(double)` renders every constant at
`std::numeric_limits<double>::max_digits10` (**17**) significant digits, so CAPD's interval
parse of the decimal literal brackets the exact double.

**Why:** The vector field CAPD integrates must be a faithful image of the symbolic RHS; the
prior `std::to_string`'s 6-digit truncation made it unfaithful by `1e-6` and produced a genuine
false `unsat` (**SOUNDNESS** — false unsat). Mechanism + the worked `1/3 → 0.999999` clock-gate
example: `docs/ode-integration.md` §Soundness. The bug was longstanding and shared by
`main`/cav26; their looser filters masked it while the tighter per-slice tube exposed it — *the
feed lied, the filter is sound*. cav26-oracle A/B evidence (123 ODE jobs): the fixed build has
**zero** false-`unsat`s, solves 118/123 vs cav26's 109, and fixes 2 of cav26's own residual
false-`unsat`s.

**Alternatives tried and rejected:** `std::to_string` (the truncating default — the bug);
scientific-notation output (CAPD's parser is unreliable on `1e-3` forms, so |v|<1e-4 or huge
values are re-rendered as `fixed`-`setprecision(40)`).

**Code:** `src/dreal/contractor/odes/to_capd_string.h` (carries
`DREAL_ASSERT_ROUNDING(FE_TONEAREST)` — decimal formatting is correctly rounded only in
nearest). Repros: `ode_soundness_repros/ws_taupin.smt2`. Tests:
`to_capd_string_test.cc::ConstantRoundTripsExactly`, `contractor_capd_test.cc::{CapdFwd,CapdBwd}`
(cumulative-gaussian witness `p=Φ(10)−Φ(-10)<1`). Mechanism detail: `docs/ode-integration.md`
§Soundness.

---

## Denormal / underflow soundness (dreal/dreal4#321)

**Decision:** A value like `2^-1075` (`pow(0.5,1075)`) underflows; it is made sound in **two
layers** — an ibex HC4-backward fork patch and a Drake constant-fold guard.

**Why:** The underflow surfaces as a genuine false `unsat` in Drake folding and as a
subnormal-scale delta-completeness imprecision in the ibex backward.

- **ibex HC4 backward** (fork patch `underflow_saturate`): a forward op soundly
  over-approximates an underflowed result to the subnormal ceiling (`pow(0.5,1075) →
  [0, DBL_TRUE_MIN]`), but the *tight* inverting backward ops (`bwd_pow`/`exp`/`sqr`/`mul`/`div`)
  were tighter than the forward and emptied a feasible operand on a subnormal-band target.
  `underflow_saturate(y)` widens a target lying entirely in the subnormal band to include 0
  before the tight inverse (keying on the endpoint farthest from 0, so `[DBL_TRUE_MIN,+inf]` is
  left alone — that keeps `bwd_div08` etc. unchanged). Siblings `bwd_sqrt`/`log`/`root` invert
  via a loose forward op and were already sound.
- **Drake constant fold** (`sound_constant_fold`): the parser builds an exactly-representable
  literal (`0.5`, integers) as a `Constant` (double), so `pow(0.5,1075)` eagerly folds via
  `std::pow` where `2^-1075` rounds to `0.0` — a lying literal making `pow(0.5,1075) > 0` a
  false `unsat`. `sound_constant_fold` (at the `pow`/`mul`/`div` fold sites) detects an
  unfaithful fold (nonzero true value underflowed to ±0, or finite overflowed to ±inf) and
  folds to a sound `RealConstant` interval bracketing the true value (`[0, DBL_TRUE_MIN]`,
  `[DBL_MAX, +inf]`, …) instead of the scalar. It stays a *constant* — a `Pow`/`Mul` with
  all-`Constant` operands violates a Drake AST invariant (`ExpressionMulFactory::AddTerm`
  asserts it) — yet is sound.

**Accepted tradeoff:** the box `underflow_saturate` declines to empty is crisp-unsat but
*delta*-sat (the true solution is within δ), so this is a delta-completeness imprecision, not a
crisp soundness bug. It can also stop legitimate pruning of robustly-unsat subnormal
infeasibility — observed once as `tacas_c2e2_0hz_k7_..._inverter_sigmoid_UNS` flipping
UNSAT→delta-sat. Acceptable by design: dReal is sound but only delta-complete, so a false
`unsat` is catastrophic while an over-permissive delta-sat is allowed. (Do not re-investigate
that flip.)

**Alternatives tried and rejected:** keeping the underflowed fold symbolic instead of a
`RealConstant` interval — aborted the Debug build via the all-`Constant` AST invariant.

**Code:** `underflow_saturate` in `../ibex-fork` (patch #12); `sound_constant_fold` in
`symbolic_expression.cc`. Tests: `ibex_log_pow_edge_cases_test.cc`,
`denorm_constant_fold_test.cc`, `gaol_directed_rounding_false_unsat_test.cc::DenormUnderflowEndToEnd`,
`denorm_underflow_smt2_test.cc`.

---

## Backward narrowing integrates −f(x), it does not swap gates

**Decision:** To narrow `X_0`, the BWD contractor integrates the genuine backward dynamics
`−f(x)` from `X_t`; it does not run the forward contractor on swapped endpoint gates.

**Why:** For a non-time-symmetric ODE the set of points in `X_t` whose *forward* trajectory
lands in `X_0` is not the backward image of `X_t` under `f`, which is what soundness for the
integral constraint demands. Narrowing `X_0` off the swapped-gate forward question can remove
valid endpoint values → false `unsat`. Every concrete `x_0 ∈ X_0` whose forward trajectory hits
`X_t` lies in the `−f(x)` backward image of `X_t`, so removing states outside that image is
sound.

**Code:** `run_capd_bwd` (`src/dreal/contractor/odes/contractor_odes_capd.cc`); FWD/BWD
direction handling in `qf_nra_ode_semantics.md` §4.5.

---

## Negated / unlinked ODE constraints are dropped, and rejection must be parse-layer

**Decision:** A negated `integral`/`forall_t` literal, and a positive `forall_t` that fails to
link to a companion integral, are silently dropped in `link_integral_invariants` — *not* turned
into a loud error. The silent drop of negated ODE atoms is the documented §6 behavior and the
root of **BUG-002** (a user-asserted negation is silently removed).

**Why:** `link_integral_invariants` runs inside the DPLL(T) loop on the SAT solver's *transient*
literal subset, where a negated ODE literal or an unlinked positive `forall_t` is a normal
product of search — so a throw there crashes valid multi-step BMC benchmarks (github
`airplane`/`gen`). Distinguishing malformed *user input* from a valid transient state needs the
global problem scope, which only the parse / `Context::Assert` layer has — so any rejection
belongs there, and is unimplemented. These drops are **COMPLETENESS** hazards (missed refutation
/ false `delta-sat`), never soundness (a removed constraint only enlarges the box). Full
transient-literal mechanism: `docs/ode-integration.md` §"Constraint forms accepted, and the
silent drops (BUG-002)".

**Alternatives tried/rejected:** throwing in `link_integral_invariants` on (a) negated ODE atoms
and (b) unlinked positive `forall_t` — both reverted after crashing legitimate benchmarks.

**Desired future semantics + roadmap:** genuine `∃t ¬φ` (negated `forall_t`); disequality vs.
definitional binding (negated `integral`); parse-layer rejection of unlinkable assertions — all
specified as aspirational `GTEST_SKIP` tests in `test/dreal/smt2/test/dreal_future.cc`. Full
mechanism: `docs/ode-integration.md` §"Constraint forms accepted, and the silent drops (BUG-002)".
