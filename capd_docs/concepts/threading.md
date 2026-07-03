# Threading (concept)

## What it is
CAPD's optional internal parallelism (`capd::threading`): a fixed-size `ThreadPool` that runs
`Task`s, a `SolverPool`/`SolverFactory` that hands each worker thread its **own** integrator
instance, and parallel wrappers `TMap` (time-map) and `PMap` (Poincaré map). The parallelism is
**data-parallel over independent integrations** — many initial conditions / sections pushed
through one shared vector field — not parallelism *within* a single adaptive Taylor step.

## Key facts (confirmed in `threading/ThreadPool.h`, `SolverFactory.h`)
- `ThreadPool(unsigned noThreads)` + `process(Task*)`; pool is non-copyable.
- Each thread gets a private solver via `SolverFactory<Solver>` / `SolverPool<Solver>` — solvers
  are not shared, so no lock contention inside an integration.
- `TMap<TM,VF>` / `PMap<PM,Section,VF>` are the parallel time-/Poincaré-map front ends.

## dReal relevance (audit answer)
**Redundant with dReal's own parallelism — no benefit, likely harmful.** dReal parallelizes at
ICP granularity: one solver per worker, and within a single ICP call each ODE constraint is
integrated sequentially. CAPD threading would *add a second pool* underneath dReal's, contending
for the same cores. It does **not** speed up a single integration (which is what an ICP call
performs); it only helps when you have many independent integrations to batch through one vector
field — a structure dReal's ICP loop does not present. Recommendation: leave CAPD threading off;
keep parallelism at the ICP level. Status: unused (`dreal-capd-usage.md`).

## Source
[group__threading.html](../../../CAPD/docs/html/group__threading.html)
