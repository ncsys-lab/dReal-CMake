# Variational equations — the flow Jacobian (monodromy)

**What it is.** Alongside the trajectory `x(t)`, CAPD can rigorously integrate the
*variational equation* — the derivative of the flow w.r.t. the initial condition,
`V(t) = ∂φ_t(x_0)/∂x_0` (the monodromy / fundamental-solution matrix). This is a C1
computation: instead of a C0 set type, you hand `timeMap` a **C1 set**
(`C1Rect2Set` / `C1HORect2Set`, doubleton representations of both the point and the matrix),
and after integration extract the matrix:

    C1Rect2Set set(initialCondition, initialTime);   // identity init for V
    IVector y = timeMap(finalTime, set);
    IMatrix monodromy = (IMatrix) set;                // V(finalTime)

**Functional form.** With a `SolutionCurve`, the monodromy is available at any intermediate
time: `timeMap(t, set, solution); solution.derivative(t)` returns `V(t)` as an `IMatrix`
(confirmed `$HDR/diffAlgebra/SolutionCurve.h:185,287`). `solution(t)` / `solution.timeDerivative(t)`
give value and time-derivative — the same eval API as a single-step `Curve`, chained over steps.

**Cost.** Step control then bounds error for *both* the main and variational equations
(documented in `odesvar_rigorous.html`), so the step sizes shrink and each step does
`O(dim²)` more coefficient work than C0 — materially more expensive than the current C0 path.

**Why it might matter to dReal.** dReal narrows the terminal box forward and the initial box
backward using only C0 enclosures. `V(t)` is exactly the sensitivity matrix an
**interval-Newton / mean-value backward step** needs: given a tight terminal constraint, the
monodromy maps terminal slack back onto the initial box `X_0` far more sharply than re-running
C0 backward integration. This is the single biggest unused capability for backward (X₀)
narrowing — but it requires switching the relevant flows to a C1 set type and feeding the
matrix into a new contraction step on dReal's side (ibex HC4 today consumes only the C0 slice
intervals).

**dReal status.** Unused. dReal uses only C0 sets (`C0Rect2Set`/`C0HORect2Set`/`C0TripletonSet`).
See `dreal-capd-usage.md`.

**Source.** [odesvar_rigorous.html](../../../CAPD/docs/html/odesvar_rigorous.html)
