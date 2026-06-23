# ODE Integration

## Background

dReal supports QF_NRA_ODE: satisfiability over nonlinear arithmetic augmented with ordinary differential equations. The input describes a hybrid system: a sequence of modes, each with continuous dynamics `d/dt[x] = f(x, t)`, connected by transition guards and resets.

The solver must decide whether a sequence of states is reachable. This requires computing **guaranteed enclosures** of ODE trajectories — interval boxes that are certain to contain all solutions starting anywhere in the initial state box.

---

## AST Nodes for ODE Constraints

ODE-related AST extensions (in `src/dreal/symbolic/symbolic.h` and the Drake-derived expression layer):

- **`Integral`**: Represents integration of a continuous flow over a time interval. `Integral(flow, t_0, t_1)` says variables evolve according to `flow` from time `t_0` to `t_1`.
- **`ForallT`**: Universal quantification over time. `ForallT(φ, t_0, t_1)` means `φ` must hold for all `t ∈ [t_0, t_1]` along the trajectory.

These are parsed from both SMT2 (`define-ode`, `integral`, `forall_t`) and dReal3-compatible `.dr` syntax (`d/dt[x] = ...`).

---

## The CAPD Integrator

**Files:** `src/dreal/contractor/odes/contractor_odes.cc` (contractor) and `contractor_odes_capd.cc` (CAPD backend)

The ODE contractor `contractor_ode_lohner` integrates flows with **CAPD**'s interval ODE solver — the sole ODE backend since the Codac elimination (see `CODAC_MIGRATION.md`). CAPD computes a **guaranteed** interval enclosure of the flow `dx/dt = f(x, t)`: a sequence of boxes certain to contain every trajectory starting anywhere in the initial box, with internal control of the **wrapping effect** (the exponential blow-up naive interval box arithmetic suffers because it cannot represent rotated or skewed sets).

### Mechanism — per-slice tube + filter

The contraction is split into a **numeric tube builder** (`contractor_odes_capd.cc`, CAPD-only) and a **box/invariant filter** (`contractor_ode_lohner::Prune`, `contractor_odes.cc`), so the CAPD layer stays purely numeric and all symbolic-box logic lives in one place. This restores the cav26 per-slice design that the Codac→CAPD rewrite had collapsed into a coarse single-endpoint intersection (an unsound over-narrowing — see *Soundness* below).

**1. Feed.** Each RHS expression is translated once into CAPD's `IMap` string format (`to_capd_string`, `to_capd_string.h`) and cached per flow in a `CapdOdeCache` — both the forward map `f(x)` and the negated map `-f(x)`. Flow **parameters** (a flow variable whose `d/dt` is the literal `0`) are emitted in the `IMap`'s `par:` section and bound per call via `setParameter` to their current box intervals — they are constant along the flow and are *not* integration variables (emitting them as variables overruns the `C0Rect2Set`'s buffers; see the `build_imap_strings` comment).

**2. Tube.** `run_capd_fwd` / `run_capd_bwd` integrate the cached `f(x)` / `-f(x)` from the start box `u0` over `[0, t_ub]` with `capd::IOdeSolver` at Taylor order `kCapdTaylorOrder = 20`, driven by `capd::ITimeMap` in `stopAfterStep` mode. Each adaptive step's rigorous Taylor curve is **sub-gridded into `kHullGrid = 16` sub-slices** (`integrate_tube_slices`), and the result is the **full time-ordered list of per-slice enclosures** `{(t_lb, t_ub), state-box}` — *not* a coarse component-wise hull. The per-slice form is what preserves the per-time / per-component correlation the filter needs; a hull would collapse it and under-refute (a false delta-sat). `IOdeSolver` instances are per call (mutable step state, not shareable across parallel ICP workers); the parsed `IMap` AD-tree is the cached, reused part.

**3. Filter** (`contractor_ode_lohner::Prune`). Walk the slices in forward-time order:
   - **Invariant** (FWD only): write each slice's state into the `ForallT` variables (a reused box copy) and run the HC4 invariant contractors. The *first* slice they empty is a trajectory-**interior** violation → every later terminal is unreachable, so stop.
   - **Terminal window**: a slice whose time overlaps the dwell window `[win_lb, win_ub]` is terminal-eligible. Intersect its state with the `X_t` gate (`m_vars_t` box) component-wise; keep the non-empty intersections.
   - **Narrow / refute**: hull the kept intersections → narrowed `X_t`, hull their times → narrowed time variable. **No surviving slice → `set_empty()`** — a sound refutation, because CAPD's enclosures are outward over-approximations, so an empty survivor set proves true infeasibility.

`run_capd_fwd` narrows `X_t` and the time variable; `run_capd_bwd` runs the symmetric filter in a swapped frame (`m_vars_0 = original X_t`) to narrow `X_0`. The theory solver queues **both** a FWD and a BWD contractor per ODE constraint (mirroring cav26's two-contractor design), so each endpoint is narrowed by its own pass — the FWD pass is also the one that enforces the `ForallT` invariant.

### Short-circuits and divergence

- **Trivial flow** (every RHS is the literal `0`, `capd_ode_cache_is_trivial`): bypass CAPD entirely and just intersect `X_0 ∩ X_t`.
- **`T = 0`** (time upper bound pinned to `0`): a zero-duration trajectory means initial = final, so intersect `vars_0[i] ∩ vars_t[i]` directly. The integration time may be a variable, a `RealConstant` interval, or an exact constant — all three are handled.
- **Divergence**: ANY CAPD exception — step-control failure (stiff tube / over-approximation explosion) *or* a mid-enclosure singularity (`capd::IntervalError` "possible division by zero", e.g. the sigmoid-inverter flows) — is caught inside `integrate_tube_slices` and reported as `found == false`; the contractor (`if (!res.found) return;`) narrows nothing for that `Prune` call (sound but incomplete). The catch is **catch-all-and-skip with no rethrow**: this architecture has no ICP-level contractor catch, so an escaping exception would `terminate()` the whole solve. (An *untranslatable* RHS is different: it raises at cache-build time — a loud failure, not a silent skip.)

### Soundness: feed faithfulness and the per-slice filter

The integrator is sound only if **two** things hold, and both were soundness bugs that have been fixed:

1. **The vector field CAPD integrates must be faithful to the true RHS.** `to_capd_string` renders every constant at `std::numeric_limits<double>::max_digits10` (17) significant digits, so CAPD's interval-parse of the decimal literal brackets the exact double. The previous `std::to_string` rendered only **6** fractional digits (`sprintf %f`), so a coefficient like `1/3 → "0.333333"` made CAPD integrate `3·(1/3)` as `0.999999` — a vector field unfaithful by `1e-6`. A clock whose terminal gate sits at the integration-window end (`tau=1` at `t=t_ub`) then has no surviving terminal slice → **false-`unsat`** (the water/thermostat automata; `ode_soundness_repros/ws_taupin.smt2`). Decimal formatting is correctly rounded only in `FE_TONEAREST`, so `to_capd_string` asserts that mode (it is always reached under `make_capd_ode_cache`'s `NearestRoundingScope`); see CLAUDE.md "FPU rounding mode".
2. **The filter must keep per-time/per-component correlation.** Intersecting a single coarse endpoint hull with `X_t` (the Codac→CAPD rewrite's form) combines `x` reached at one time with `p` reached at another → a false delta-sat on anti-correlated tubes, or a missed interior invariant violation. The per-slice filter above keeps the correlation. Regression coverage: `contractor_odes_semantic_test.cc` (`GravityInvariantTest`, `AntiCorrelatedTest`, `DecayFlowTest.*`) and `contractor_capd_test.cc` (`CapdFwd`/`CapdBwd` — the cumulative-gaussian witness `p=Φ(10)−Φ(-10)<1`).

---

## Trajectory Visualization

When `--visualize` is set, the ODE code generates trajectory enclosures via `run_capd_trace` (`contractor_odes_capd.cc`), driven by the contractor's `generate_trace`. It integrates the cached flow over `[0, t_ub]` and records an over-approximating box for each of `n_steps` equally-spaced sub-slices — the full tube of all trajectories, not just the contracted endpoint boxes. `forward = true` integrates `f(x)`; `forward = false` integrates `-f(x)` for a reverse-time view. If the integrator diverges before reaching `t_ub`, the slices recorded up to that point are still emitted (partial traces stay visualizable). The output is dumped as JSON for downstream plotting.

---

## Mode Sequence Encoding

A hybrid system with `k` modes is encoded as a formula over `k` copies of the state variables, one per mode. The solver searches for an assignment of initial/final states for each mode such that:
- The final state of mode `i` matches the initial state of mode `i+1` (after applying the reset map).
- The guard condition for the transition holds.
- For each mode, the ODE constraint `Integral(flow_i, 0, τ_i)` is consistent with the assigned initial and final states.

The benchmark `bouncing_ball_with_drag_10_0.smt2` is a 10-mode bouncing ball — each bounce is one mode. The solver must find a trajectory that satisfies all 10 modes simultaneously.

---

## Performance

CAPD became the sole ODE backend after benchmarking confirmed it was at or below the old Codac `CtcLohner` runtime on the tested ODE families (cardiac, prostate, bouncing-ball). The Taylor order is `kCapdTaylorOrder = 20`: the per-slice tube sub-grids `kHullGrid = 16` enclosures *per adaptive step*, so cost scales with the step count — a low order takes many small steps and the `16×` explodes (the k256 thermostat went 187 s → timeout at order 10, back to 196 s at order 20). Order 20 takes fewer, larger steps and its tighter per-step enclosure also localizes interior invariant violations better. Lowering order or loosening tolerance only *widens* a sound enclosure (never a false-`unsat`). Per-flow caching of the parsed `IMap` (built once, reused across every `Prune`) keeps steady-state integration off the expression-translation path.

See `CODAC_MIGRATION.md` for the headline benchmark table and `OPTIMIZATION_LOG.md` for the order-tuning and full optimization timeline.

---

## CAPD build wiring

CAPD runs on ARM64 via `CAPD_INTERVAL_TYPE=NATIVE` (master SHA `b353e170`), which uses CAPD's own `DoubleRounding` and skips FILIB entirely. See `DEPENDENCIES.md` § "CAPD" for the full build-wiring details. CAPD expects the FPU in `FE_TONEAREST`; `contractor_ode_lohner::Prune` establishes a nested `NearestRoundingScope` for the CAPD work (and a further nested `UpwardRoundingScope` around the ibex invariant sub-contractors it calls), and the `run_capd_*` / `make_capd_ode_cache` adapters open an `ExpectClobber` `NearestRoundingScope` to contain CAPD's directed-mode clobber (CAPD leaves the FPU in a directed mode on return rather than restoring nearest) — see CLAUDE.md "FPU rounding mode". (The earlier Codac/CAPD gated hybrid, and the `--capd-t-gate` / `--capd-ndim-gate` flags that selected between them, were retired when Codac was removed.)

---

## Dependency history

The current CAPD-only design is the result of two migrations: an earlier move to a Codac-based ODE contractor, then the **removal** of Codac (Codac and Eigen are no longer dependencies). The current stack source-builds the IBEX fork (`ncsys-lab/ibex-lib@dreal-perf-patches`) and CAPD (`CAPDGroup/CAPD@b353e170`); ODE boundary values cross the interface as `ibex::Interval` / `ibex::IntervalVector`. See `DEPENDENCIES.md` for the current stack and `CODAC_MIGRATION.md` for the full migration narrative.

---

## Input Formats

### SMT2-LIB ODE syntax

```smt2
(define-ode flow_1 ((= (D 0 x) (- x)))
                   ((= (D 0 v) (+ (* -1.0 (sin x)) (* -0.5 v)))))

(assert (and (<= 0.0 time_0_1) (<= time_0_1 3.0)
             (= [x_1_0 x_1_t] (integral 0 time_0_1 [x_0_0 x_0_t] flow_1))
             (forall_t 1 [0 time_0_1] (<= x_1_t 2.0))))
```

### dReal3-compatible .dr syntax

```dr
{
  mode 1;
  invt:
    (x >= 0);
  flow:
    d/dt[x] = v;
    d/dt[v] = -9.8;
  jump:
    (x = 0) and (v <= 0) ==> @2 (x' = x) and (v' = -0.9 * v);
  init:
    @1 (x >= 1) and (x <= 1.5);
  goal:
    @2 (x >= 0.5);
}
```

Both formats ultimately produce the same `Integral` and `ForallT` AST nodes in the symbolic layer.
