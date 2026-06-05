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

The contractor runs `CtcLohner` with:
- `TimePropag::FWD_BWD`: 5 alternating forward/backward contractions
- 50 integration steps per contraction pass
- Taylor order 2 (Codac default; not configurable without patching Codac)

The `FWD_BWD` mode contracts both the initial conditions (backward from the final state) and the final state (forward from the initial conditions), squeezing the tube from both ends.

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

| Backend | Platform | Time on `bouncing_ball_10_0` | Taylor order |
|---|---|---|---|
| CAPD v4 (old, ncsys-lab) | x86 Rosetta on ARM64 | ~0.5 s | 20 |
| Codac v2 CtcLohner (current) | ARM64 native | ~13 s | 2 |

The 26× regression is due to:
1. **Taylor order**: CAPD used order 20 (much tighter enclosures per step, fewer bisections). Codac's `CtcLohner` is fixed at order 2.
2. **Architecture**: Native ARM64 vs. x86 emulation — the architectural advantage partially offset the algorithmic difference.

This regression is accepted on the `upgrade-ibex` branch because the research focus (CAV26) is pattern-matching / lemma reuse, not ODE integration speed. See `CODAC_MIGRATION.md` for a documented path to fix this via CAPD v6 ARM64 if ODE performance becomes critical.

---

## What Was Replaced

The old stack:

- `ncsys-lab/ibex-lib` — IBEX fork with CAPD interval arithmetic headers
- `ncsys-lab/capdDynSys-4.0` — CAPD v4 interval ODE library
- `contractor_odes.cc` (old) — CAPD-based contractor

The new stack:

- `lebarsfa/ibex-lib@ibex-2.8.9.1` — Standard IBEX fork (no CAPD dependency)
- `codac-team/codac@v2.0.2` — Codac v2 with `CtcLohner`
- `contractor_odes_codac.cc` — Current implementation

The main difficulty in the migration was that Codac v2 uses IBEX's standard interval arithmetic internally, but the old contractor used CAPD's interval types. All interval conversions at the boundary now go through IBEX's `ibex::Interval` / `ibex::IntervalVector`.

---

## CAPD v6 ARM64 Path (Not Taken)

CAPD v6 was investigated as a way to recover Taylor order 20 performance on ARM64. The blocker was FILIB (a low-level interval library), which depends on x86-specific FPU control (`fenv.h` intrinsics for directed rounding that have no ARM64 equivalent in FILIB's implementation). Replacing FILIB with a portable alternative would require significant CAPD patching.

The decision to use Codac instead of fixing FILIB is documented in `CODAC_MIGRATION.md`. If this path needs to be revisited, the key file is `capd/filib/interval.h` in CAPD's source.

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
