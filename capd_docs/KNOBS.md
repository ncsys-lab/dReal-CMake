# CAPD ODE tuning knobs — the complete surface (so none gets missed)

This is the **audit-complete enumeration** of every tunable in CAPD's rigorous-ODE
path, derived from the solver's public setter surface
(`grep 'set\|turnOn\|turnOff' dynsys/*.h`) plus the two template-parameter menus
(the **set type** and the **QR/reorg policy**). Each row says what the knob is,
the size of its option menu, what dReal does with it today, and where it is
documented in full. The point: a knob can only be "left on the table" if it is
missing from this table — so this table is the thing to keep complete.

Soundness note: with one exception (FPU rounding, handled by dReal's own scopes +
`-frounding-math`), **none of these knobs can cause a false `unsat`** — they trade
enclosure tightness (completeness) and speed. A coarser/abandoned setting only
risks a missed refutation, never an unsound one.

## The knob surface

| Knob | Kind | Option menu | dReal today | Full detail |
|---|---|---|---|---|
| **Set representation** | menu (template) | **~25 types** (C0/C1/C2/Cn × Ball/Affine/Doubleton/Tripleton/HO) | 3 reachable via `--ode-c0-set` (`C0Rect2Set` default, `C0TripletonSet`, `C0HORect2Set`) | [classes/sets/COMPARISON.md](classes/sets/COMPARISON.md) |
| **QR / reorganization policy** | menu (template) | **~4 QR × ~7 reorg** | hard-wired `Rect2` (FullQR + FactorReorg); not a flag | [classes/sets/QRPolicies.md](classes/sets/QRPolicies.md) |
| **Step-control policy** | menu (`setStepControl`) | **5 policies** (`ILastTerms`/`IEncFound`/`Fixed`/`No`/`DLastTerms`) | default `ILastTermsStepControl(_terms=1)` only | [classes/solvers/StepControl.md](classes/solvers/StepControl.md) |
| **Taylor order** | scalar (`setOrder`) | int, ~3–30 (problem-dependent) | `--ode-taylor-order` 12 / `--ode-backward-order` 12 | [concepts/taylor-method.md](concepts/taylor-method.md), [classes/solvers/OdeSolver.md](classes/solvers/OdeSolver.md) |
| **Absolute tolerance** | scalar (`setAbsoluteTolerance`) | real | `--ode-abs-tol` 1e-10 | [dreal-capd-usage.md](dreal-capd-usage.md) |
| **Relative tolerance** | scalar (`setRelativeTolerance`) | real | `--ode-rel-tol` 1e-10 | [dreal-capd-usage.md](dreal-capd-usage.md) |
| **Max step cap** | scalar (`setMaxStep`) | real (0 = adaptive) | `--ode-max-step` 0 | [classes/solvers/OdeSolver.md](classes/solvers/OdeSolver.md) |
| **Min step floor** | scalar (in the step-control policy) | real (default ≈ 9.54e-7) | default only (not exposed) | [classes/solvers/StepControl.md](classes/solvers/StepControl.md) |
| **Manual fixed step** | scalar (`setStep` + `FixedStepControl`) | real | unused (adaptive) | [classes/solvers/StepControl.md](classes/solvers/StepControl.md) |
| **Cn derivative mask** | menu (`setMask`) | per-partial on/off | unused (C0 only) | [classes/solvers/CnOdeSolver.md](classes/solvers/CnOdeSolver.md) |
| **Affine IC seeding** | input shape (`setInitialCondition`) | `IVector` vs affine `(x,C,r0)` | seeds from `IVector` (axis-aligned) | [classes/solvers/OdeSolver.md](classes/solvers/OdeSolver.md) |
| **Flow parameters** | input (`setParameter`) | per-param bind | **used** (params bound per ICP call) | [classes/maps/Map.md](classes/maps/Map.md) |
| **Interval backend** | build flag (`CAPD_INTERVAL_TYPE`) | NATIVE / FILIB / Mp | NATIVE + `-frounding-math` (mitigated) | [concepts/intervals-and-rounding.md](concepts/intervals-and-rounding.md) |

## Coverage status (the audit of this doc set)

- **Set representation** — ✅ complete (all ~25 types tabulated). *Was selective in
  the first pass (7 of 25); now complete.*
- **QR / reorganization policy** — ✅ now complete ([QRPolicies.md](classes/sets/QRPolicies.md)).
  *This was the second selective gap: the sets wire only the `Rect2`/`Pped2`/`Intv2`
  combos, but the policy menu is much larger and composable.*
- **Step control** — ✅ complete (all 5 policies + recommendations).
- **Scalar knobs** (order, tolerances, max/min step, fixed step) — ✅ documented;
  these are single-number levers, not menus, so a table row + the solver/step nodes
  suffice. Order is the one with a real problem-dependent sweet spot (tacas ~8–12,
  stiff github ~16–20) — see `OPTIMIZATION_LOG.md`.
- **Cn mask / affine IC seeding** — documented as future/structural (tie to the C1
  work in [AUDIT.md](AUDIT.md) tier A/D).

## Cross-cluster enumeration audit (method + result)

To make completeness *auditable* rather than asserted, every cluster's full public
surface was enumerated mechanically from the headers and diffed against the nodes.
Re-run any row to re-audit:

| Cluster | Enumeration command (from `gcc_build/capd-install/include/capd/`) | Result |
|---|---|---|
| Sets | `grep -E 'typedef.*Set;' dynset/typedefs.h` | ✅ all documented (COMPARISON.md) |
| QR/reorg policies | `grep -E 'class.*QR\|class.*Pivoting' dynset/QRPolicy.h; ls dynset/reorganization/` | ✅ all documented (QRPolicies.md) |
| Step control | `grep -E 'class.*StepControl' dynsys/StepControl.h` | ✅ all 5 documented (StepControl.md) |
| Solvers | `grep -E 'class.*OdeSolver' dynsys/*.h` ; `grep -E 'encloseC[0-9n]Map' dynsys/*.h` | ✅ all + every `enclose*Map`, **except `Fad*`** (see below) |
| Curves | `grep -E 'class.*Curve' diffAlgebra/*.h` ; Curve method list | ✅ all methods/classes, **except `FadCurve`** |
| Poincaré / sections | `grep -E 'class.*Section\|PoincareMap\|TimeMap' poincare/*.h` | ✅ all four section types + maps |
| DiffIncl | `grep -E 'class.*DiffInclusion\|MultiMap' diffIncl/*.h` | ✅ all (CW, LN, MultiMap) |
| Infra (matrix/round/thread) | `grep -E 'Inverse\|QR_decompose\|orthonormalize\|matrixExp\|spectralRadius' matrixAlgorithms/*.h` | ✅ all C1-prereq functions + rounding + threading |

**The one omission the audit found — deliberately excluded, not missed:** the
**`Fad*` family** (`FadOdeSolver`, `BasicFadOdeSolver`, `FadCurve`) — CAPD's
**forward-automatic-differentiation, NON-rigorous (floating-point, not interval)**
solver/curve. It is correctly out of scope: dReal needs *rigorous interval*
enclosures for soundness, and a non-interval integrator cannot provide them. It is
recorded here so the exclusion is explicit rather than a silent gap. (Same status
as the multiprecision `Mp*` sets: real CAPD capability, inapplicable to dReal's
build — `CAPD_INTERVAL_TYPE=NATIVE`, rigorous.)

## Where the leverage is (cross-ref [AUDIT.md](AUDIT.md))

Ranked by value/effort, the knobs most likely to be "leaving performance on the
table":
1. **Set type** (cheap): `--ode-c0-set` default → tripleton; wire `C0HOTripletonSet`. *AUDIT B1–B2.*
2. **QR/reorg policy** (cheap, currently unexposed): `SelectiveQRWithPivoting` for
   speed on well-conditioned frames; the reorganization `factor`. *AUDIT B5; details in [QRPolicies.md](classes/sets/QRPolicies.md).*
3. **Step-control policy** (cheap): `ILastTermsStepControl(2–3)` or `IEncFoundStepControl`. *AUDIT B3.*
4. **C1 set + variational** (structural): the one big tightness lever. *AUDIT A.*
