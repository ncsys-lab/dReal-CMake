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

## The Lohner Method

**File:** `src/dreal/contractor/odes/contractor_odes_codac.cc`

The current ODE contractor uses Codac v2's `CtcLohner`, which implements the Lohner interval method.

### How Lohner Works

The Lohner method computes a **wrapping-free** interval enclosure of an ODE flow `dx/dt = f(x, t)` over a time step `[t, t+h]`:

1. **Predictor step**: Use a Taylor series in time (order 2 in the current Codac configuration) to compute a rough enclosure `Ã` of all trajectories starting in box `B₀`.
2. **Corrector step**: Use the Lohner representation (a parallelotope or zonotope centered at the midpoint) to tighten `Ã` while bounding the wrapping error.
3. **Verification**: Prove the enclosure is valid using the Picard operator: `Φ(X) = x₀ + ∫₀ʰ f(X(s), s) ds`. The fixed point of Φ is the true solution set.

The key advantage over naive interval Euler methods is control of the **wrapping effect** — intervals tend to grow exponentially with naive box arithmetic because they cannot represent rotated or skewed sets. Lohner's parallelotope representation bounds this growth.

### Configuration in dReal

The Codac contractor runs `CtcLohner` with:
- `TimePropag::FWD_BWD`: 2 alternating forward/backward contractions (`contractions=2`)
- Adaptive `n_steps = clamp(max(20, ceil(t_ub * 2)), 20, 60)` — keeps a 20-step floor for short horizons, adds steps when `t_ub > ~10`
- Taylor order 2 (Codac default; not configurable without patching Codac)

The `FWD_BWD` mode contracts both the initial conditions (backward from the final state) and the final state (forward from the initial conditions), squeezing the tube from both ends.

For long-horizon or high-dimensional flows the CAPD backend (order-20 Taylor) fires instead — see "CAPD gated hybrid" below.

---

## Trajectory Visualization

In addition to contraction, the ODE code generates trajectory enclosures for visualization using `LohnerAlgorithm` directly (not `CtcLohner`). This produces a sequence of boxes representing the tube of all trajectories over the time domain. The output is dumped as JSON for downstream plotting when `--visualize` is set.

The `LohnerAlgorithm` call uses the same parameters as `CtcLohner` but produces the full tube rather than just contracting the endpoint boxes.

---

## Mode Sequence Encoding

A hybrid system with `k` modes is encoded as a formula over `k` copies of the state variables, one per mode. The solver searches for an assignment of initial/final states for each mode such that:
- The final state of mode `i` matches the initial state of mode `i+1` (after applying the reset map).
- The guard condition for the transition holds.
- For each mode, the ODE constraint `Integral(flow_i, 0, τ_i)` is consistent with the assigned initial and final states.

The benchmark `bouncing_ball_with_drag_10_0.smt2` is a 10-mode bouncing ball — each bounce is one mode. The solver must find a trajectory that satisfies all 10 modes simultaneously.

---

## Performance

`bouncing_ball_with_drag_10_0.smt2` (10 modes) runs at ~4.2 s on ARM64 native under Lohner-only (below the default `--capd-t-gate 5.0`), vs ~0.5 s on the old CAPD order-20 backend (x86 Rosetta). The gap on this benchmark is a Lohner-order-2 ceiling; CAPD (see below) is intended to close it on long-horizon flows where `t_ub` exceeds the gate.

See `CODAC_MIGRATION.md` for the headline benchmark table and full optimization timeline.

---

## CAPD gated hybrid

`contractor_odes_capd.{h,cc}` provides a second ODE backend using `capd::IOdeSolver` (order 20) + `capd::ITimeMap`. It fires when `t_ub > --capd-t-gate` (default `5.0`) or `n_state_vars >= --capd-ndim-gate` (default `6`). On divergence it falls back to Lohner. The trivial-flow short-circuit takes precedence over the gate.

CAPD runs on ARM64 via `CAPD_INTERVAL_TYPE=NATIVE` (master SHA `b353e170`), which uses CAPD's own `DoubleRounding` and skips FILIB entirely. See `DEPENDENCIES.md` § "CAPD" for the full build-wiring details and `CODAC_MIGRATION.md` § "CAPD-Lohner gated hybrid" for the dispatch policy.

---

## What was replaced

`ncsys-lab/ibex-lib` + `ncsys-lab/capdDynSys-4.0` + `contractor_odes.cc` (old, CAPD-based) → `lebarsfa/ibex-lib` (prebuilt `ibex-2.8.9.20250626`) + `codac-team/codac@v2.0.2` + `CAPDGroup/CAPD@b353e170` + `contractor_odes_codac.cc` / `contractor_odes_capd.cc`. Per-library rationale lives in `DEPENDENCIES.md`. The boundary conversions now go through `ibex::Interval` / `ibex::IntervalVector` since Codac v2 shares IBEX's interval arithmetic.

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
