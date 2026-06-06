# Codac v1 — CtcEval

Source: https://codac.io/v1/manual/05-dynamic-contractors/02-ctc-eval.html (fetched 2026-06-05)

## Constraint

y_i = x(t_i): an observation `y_i` at time `t_i` is a point-evaluation of trajectory `x`.

## Inputs

- `[t_i]` — interval of observation time
- `[y_i]` — interval of observed value
- `[x](·)` — trajectory tube
- `[v](·)` — derivative tube

Both `x` and `v` need matching slicing, t-domain, dimension.

## Performance helpers

- `.enable_time_propag(false)` — restrict contraction to the observation slice (faster when stacking many observations)
- Apply in a fixpoint loop for multiple observations

## Why this doesn't help dReal's ODE workload

We already pin the endpoint gates via `tube.set(X0, 0)` and `tube.set(Xt, t_ub)` in `run_lohner_integration`. CtcEval would do the same intersection but it also expects a slice-derivative tube `v` we don't have, and it propagates side-effects to the rest of the tube (which we don't use — we only read the endpoint slices). No incremental value.
