# Follow-up plan: nlopt local-search seeding for off-center SAT instances

**Status: IMPLEMENTED as `--seed-local`** (`src/dreal/solver/seed.{h,cc}`, 2026-06). This file is
the original design rationale; the built result, A/B benchmarks, and conclusions are in
`benchmark/optsearch/SEARCH_LOG.md` §"Seed-and-verify" and `docs/decisions.md` §"Seed-and-verify".

Key outcomes vs this plan: (1) per the "why not just sampling?" pivot, **LHS sampling is the
default proposer** (`--seed-method lhs`) and nlopt the alternative (`--seed-method nlopt`) — both
feed one verify hook; (2) the winner is **multi-start nlopt** (`--seed-samples 64`): on odeexpr it
solves +2 over the 0.56 magic at 3× lower PAR2, zero soundness flips, no overhead regressions,
because guided search needs ~1000× fewer candidates than blind LHS on tiny feasible regions;
(3) nlopt needed three structural fixes for the CSE-heavy boxes (unbounded-dim sub-box, CSE
substitution, NNF objective); (4) the flat-center risk is real — a single center COBYLA start
stalls, ≥2 multi-starts dodge it.

Spun out of the 2026-06 branching investigation
(`benchmark/optsearch/SEARCH_LOG.md` §"Why branching matters here, and why no order is robust";
`docs/decisions.md` §"Branching split-ratio 0.56 is a symmetry-break").

## Why

ICP branch-and-prune is structurally weak at one thing: finding a single **off-center** solution
in a landscape that is **flat (∇=0) at the symmetric center**. The symmetric Lyapunov SAT
instances (`tanh_decrease__*`) are exactly this case — the feasible region sits off-center while
the constraint is flat near the origin where the search dwells, so bisection must subdivide many
dimensions (22.8M nodes on J1.0 at midpoint). The `--split-ratio 0.56` symmetry-break is a
gradient-free workaround that is *brittle* (J0.6 still 24.7M nodes) and not robust across the order
space. The principled fix is to find the off-center point **directly** with local optimization,
not blind bisection. dReal already links nlopt and uses it for ∃∀ counterexample refinement.

## Idea (seed → verify; nlopt proposes, the sound machinery disposes)

For an NRA `CheckSat` whose box hasn't yet contracted to a delta-box:
1. Build an objective = total constraint violation at a point (reuse the point-evaluation idea from
   the reverted `CenterInfeasibility`, or `cached_expression`), and minimize it with
   `NloptOptimizer` (`src/dreal/optimization/nlopt_optimizer.h`, `Optimize(&x, &opt_f)`).
2. If nlopt returns a near-feasible point `x*` (`opt_f` below a threshold), construct a small box
   `B` centered at `x*` (width ~ precision) and run the **existing** contractor + `EvaluateBox`
   (`src/dreal/solver/icp.cc`) on `B`.
3. If `B` evaluates delta-SAT, return delta-sat immediately. Otherwise continue normal ICP.

**Soundness is free:** the delta-sat verdict is established **only** by the existing sound
`EvaluateBox` on `B` — nlopt merely proposes a where-to-look. A poor seed cannot cause a false
`delta-sat`. This is a *speed optimization atop the complete ICP search* (like the cache carve-out:
the canonical complete path still runs), **not** a fallback that substitutes degraded behavior for
a failure — frame it that way to stay within the no-silent-fallbacks rule.

## Where to hook

- **Pre-pass** in `IcpSeq::CheckSat` (or `TheorySolver`) before the bisection loop — simplest;
  one local-opt per theory call.
- or **periodic** seeding every N nodes — opportunistic, costs more eval.
- Gate to NRA SAT-likely instances; **skip ODE/forall** (forall already uses nlopt internally;
  per-node nlopt over ODE flows is too costly). `config.h` already has `use_local_optimization`
  (exist-forall only today) — extend it or add `--seed-local`.

## Reuse (don't rebuild)

- `NloptOptimizer` + `cached_expression` (`src/dreal/optimization/`).
- The forall counterexample-refiner (`src/dreal/contractor/counterexample_refiner.cc`) is the
  closest existing pattern: it already minimizes a violation objective with nlopt over a box.
- `EvaluateBox` for the verify step.

## Validation

- **Transformative benchmarks:** should crack `tanh_decrease__J1.0/J0.6` directly (local-opt finds
  the off-center witness; verify a small box) — the case ICP-order can't robustly solve.
- **Cross-family:** zero SAT↔UNSAT flips (soundness — the seed-box must pass real `EvaluateBox`);
  no slowdown where seeding doesn't fire (gate it cheaply); measure the per-call nlopt cost so it
  doesn't tip borderline benchmarks (the smear/ACID failure mode).
- **Soundness probe:** a curated instance where local-opt finds a spurious near-feasible point that
  is *not* delta-SAT — confirm the verify step rejects it (no false `delta-sat`).

## Open questions

- Pre-pass vs periodic; the `opt_f` acceptance threshold; seed-box width vs `precision`.
- Smooth surrogate objective for strict inequalities / `max` of violations.
- Whether to seed from the box center or a few restarts.
