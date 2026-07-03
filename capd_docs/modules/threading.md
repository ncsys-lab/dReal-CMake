# threading (module)

## What it is
CAPD's internal parallelism for running the **same** integrator over many initial
conditions/sets concurrently: a fixed-size thread pool plus parallel wrappers around time-maps
and Poincaré maps, in `capd::threading` (`threading/ThreadPool.h`, `SolverPool.h`,
`SolverFactory.h`).

## Key API (from group__threading.html, confirmed in `threading/`)
- `class ThreadPool` — fixed-size pool; `explicit ThreadPool(unsigned noThreads)`,
  `void process(Task* task)`. Non-copyable/non-movable.
- `class Task` (abstract) + `LambdaTask<F>` with `static Task::createFromLambda(F&)`.
- `struct SolverFactory<Solver>` / `DefaultSolverFactory<Solver>` — abstract factory producing
  one solver instance per worker thread (each thread gets its own stepper; solvers are not
  shared).
- `struct SolverPool<Solver>` — pool of per-thread solver instances.
- Parallel map wrappers: `struct TMap<TM, VectorFieldType>` (parallel time-map),
  `struct PMap<PM, Section, VectorFieldType>` (parallel Poincaré map), with
  `TMapFactory`/`PMapFactory` and the shared base `BaseTPMap<S,VF>`.

## dReal status
**Unused.** Not on dReal's referenced-symbol list (`dreal-capd-usage.md`); dReal's parallelism is
ICP-level (one solver per worker thread, managed by dReal itself). CAPD threading is explicitly
out of scope there.

## Why it might matter (audit)
This is the "intra-integration parallelism" the audit asks about. The model is data-parallel over
*independent integrations* (many ICs through one vector field), not parallelism *within a single
adaptive Taylor step*. dReal already parallelizes at ICP granularity, so CAPD threading would be
**redundant and likely contend** with dReal's worker pool — a single ODE-constraint integration
inside one ICP call is sequential and CAPD threading does not speed it up. No benefit unless dReal
restructured to batch many sub-problems through one shared vector field, which the ICP loop does
not currently do.

## Source
[group__threading.html](../../../CAPD/docs/html/group__threading.html)
