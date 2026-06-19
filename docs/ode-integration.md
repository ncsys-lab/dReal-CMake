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

### Mechanism

Each RHS expression is translated once into CAPD's `IMap` string format (`to_capd_string`, `to_capd_string.h`) and cached per flow in a `CapdOdeCache` — both the forward map `f(x)` and the negated map `-f(x)`. Integration uses `capd::IOdeSolver` at Taylor order `kCapdTaylorOrder = 10` driven by `capd::ITimeMap` over `[0, t_ub]`. `IOdeSolver` chooses its step size adaptively; instances are constructed per call (they carry mutable step state and so cannot be shared across parallel ICP workers — unlike Codac's `const CtcLohner::contract`).

Two integration entry points (`contractor_odes_capd.h`):

- **`run_capd_fwd`** — integrate the cached `f(x)` forward from the initial box `X_0`, intersect the terminal enclosure with `X_t` (→ narrowed terminal state), then integrate the cached `-f(x)` backward from the narrowed terminal to narrow `X_0`. A single call narrows **both** endpoints — recovering the joint narrowing that Codac's `CtcLohner` FWD_BWD did, in one pass.
- **`run_capd_bwd`** — a one-shot backward image: integrate `-f(x)` from `X_t`; the terminal enclosure is the set of time-0 states whose forward trajectory reaches `X_t`. The caller intersects it with the current initial bounds. One-way narrowing.

Flow **parameters** (a flow variable whose `d/dt` is the literal `0`) are bound into a private copy of the cached `IMap` via `setParameter` before integration; they are not integration variables.

### Short-circuits and divergence

- **Trivial flow** (every RHS is the literal `0`, `capd_ode_cache_is_trivial`): bypass CAPD entirely and just intersect `X_0 ∩ X_t`.
- **`T = 0`** (time upper bound pinned to `0`): a zero-duration trajectory means initial = final, so intersect `vars_0[i] ∩ vars_t[i]` directly.
- **Divergence**: if CAPD's step control fails (stiff tube / over-approximation explosion), `run_capd_fwd`/`run_capd_bwd` catch the integrator exception internally and report `CapdOdeResult::found == false`; the contractor (`if (!res.found) return;`) then narrows nothing for that `Prune` call — sound but incomplete. (An *untranslatable* RHS is different: it raises at cache-build time.)

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

CAPD became the sole ODE backend after benchmarking confirmed it was at or below the old Codac `CtcLohner` runtime on the tested ODE families (cardiac, prostate, bouncing-ball); the Taylor order was later tuned 20 → 10 (`kCapdTaylorOrder`). Per-flow caching of the `IMap` (built once, reused across every `Prune`) keeps steady-state integration off the expression-translation path.

See `CODAC_MIGRATION.md` for the headline benchmark table and `OPTIMIZATION_LOG.md` for the order-tuning and full optimization timeline.

---

## CAPD build wiring

CAPD runs on ARM64 via `CAPD_INTERVAL_TYPE=NATIVE` (master SHA `b353e170`), which uses CAPD's own `DoubleRounding` and skips FILIB entirely. See `DEPENDENCIES.md` § "CAPD" for the full build-wiring details. CAPD expects the FPU in `FE_TONEAREST`; `contractor_ode_lohner::Prune` establishes that mode internally and restores `FE_UPWARD` on exit — see CLAUDE.md "FPU rounding mode". (The earlier Codac/CAPD gated hybrid, and the `--capd-t-gate` / `--capd-ndim-gate` flags that selected between them, were retired when Codac was removed.)

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
