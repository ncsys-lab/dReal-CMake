# Chapter: Optimizer (`ibexopt`)

Source: [`optim.rst.txt`](../../../ibex-docs/_sources/optim.rst.txt) ·
`optim.html`. The IBEX `Optimizer` — rigorous global optimization (minimize a goal
subject to constraints) via interval branch-and-bound with the linear relaxation +
KKT machinery. Driver: `ibexopt`. Programming API: [optim-prog](optim-prog.md).

> **dReal status:** **not used.** dReal is a satisfiability solver, not an
> optimizer; for objective-bearing problems it uses **nlopt** (a separate local
> optimizer, `--nlopt-*` flags) rather than IBEX's rigorous `Optimizer`.

**Capability-extension note (non-perf):** if dReal ever wants *rigorous*
optimization-modulo-theories (a sound global bound, not nlopt's local one),
IBEX's `Optimizer` + `ExtendedSystem` + KKT contractor (`CtcKuhnTucker`) is the
IBEX path. Tracked in [`../AUDIT.md`](../AUDIT.md) "capability extensions". This is
also why dReal can ignore most of the [system](system.md) module's transforms
(normalize/extend/KKT) — they exist to serve this optimizer.
