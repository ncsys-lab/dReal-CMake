# Maps and autodiff — the vector field as an AD DAG

**What it is.** A CAPD `IMap` is the parsed, differentiable representation of an ODE's
right-hand side `f(t, x; par)`. Construction parses a string (or a C-routine of
`autodiff::Node`s) into an automatic-differentiation DAG; thereafter the same object computes,
on demand, `f`, its Jacobian `Df`, Hessian, jets, and — for ODE solving — the recursive
Taylor coefficients of the *solution* via `computeODECoefficients`. The AD engine
(`group__autodiff`) supplies one Taylor-recurrence rule per elementary operation.

## Construction and the parse cost

- String form: `"[par:...;][time:t;]var:...;fun:...;"`. The parse builds `m_fullGraph`, then
  `createEvalPath` prunes to `m_evalPath` (only nontrivial nodes). This DAG build is the
  per-flow setup cost.
- C-routine form: pass `void vf(Node t, Node in[], ..., Node out[], ..., Node par[], ...)`.
  Documented (`maps.html`) as giving **identical evaluation performance** — it differs only in
  *how the DAG is built* (no text parse), useful for large expressions.
- `setParameter(d, v)` / `setParameters(...)` mutate parameter values **without re-parsing**;
  `operator=`/`reset` re-parse and reallocate the DAG.

## Value/derivative evaluation

`f(x)` via `operator()(u)`; simultaneous value+Jacobian via `f(u, Df)` (faster than two
calls); Hessian via `f(x, Df, Hf)`; jet propagation via `f(jet)`. The `computeODECoefficients`
overloads optionally also emit first/second variational coefficients — the data the C1/C2 ODE
solvers consume (see `variational-equations.md`).

**dReal status.** `IMap` construction (string) + `setParameter` rebinding used; cached once per
flow (`CapdOdeCache`, `fn_fwd`/`fn_bwd`). Direct Jacobian/Hessian/jet evaluation and the
variational `computeODECoefficients` overloads unused. See `dreal-capd-usage.md`.

**Why it might matter.** Parse/DAG build is already amortized by caching, so the parse path is
not a per-call bottleneck. The `Node` C-routine path offers no evaluation-speed win (docs say
identical) — only a construction-time alternative. The unused Jacobian-evaluation API is what a
C1 backward-narrowing step would call.

**Source.** [maps.html](../../../CAPD/docs/html/maps.html),
[group__map.html](../../../CAPD/docs/html/group__map.html)
