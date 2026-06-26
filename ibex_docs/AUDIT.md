# AUDIT.md — where dReal could leverage IBEX better

The payoff of the crawl: prioritized, header-grounded opportunities for dReal to
use IBEX more, each cross-referenced to current usage
([`dreal-ibex-usage.md`](dreal-ibex-usage.md)) and the nearest fork patch.

> **Companion deep-dives (second pass):** the full contractor catalog with
> strengths/weaknesses is
> [`classes/contractors/COMPARISON.md`](classes/contractors/COMPARISON.md); the
> bisector catalog is [`classes/strategy/Bisectors.md`](classes/strategy/Bisectors.md);
> and the "does dReal needlessly reimplement IbexSolve?" question is answered
> (with file:line on both sides) in
> [`ARCHITECTURE-COMPARISON.md`](ARCHITECTURE-COMPARISON.md). Their net conclusion
> *reinforces* this audit: the search loop is correctly dReal's own, and the one
> real loop-level gap is precisely tier A below.
>
> **Second audit section — nested quantifiers:** for ∃∀ / `∀∃∃∀` queries over
> high-dimensional transcendental constraints (a distinct workload), see the focused
> **[`AUDIT-QUANTIFIERS.md`](AUDIT-QUANTIFIERS.md)** — how IBEX's *composable*
> `CtcForAll`/`CtcExist` contractors can pre-prune dReal's CEGIS loop and provide a
> sound skeleton for the deeper alternations dReal currently crashes on. (Its top
> lever, Q3, is tier A again — a strong inner contractor compounds super-linearly when
> the cost is exponential in the quantified dimension.)

**The one-paragraph framing (the crawl's main finding).** By default, dReal's
*only active IBEX contractor is HC4* (`Function::backward`). The polytope/X-Taylor
path is present but **dormant** (`--polytope` off, `LP_LIB=none`). And the
micro-optimization budget of the HC4 path is **already spent** — fork patches
already inlined the rounding-mode toggles (#9, 23–44%), batched the rounding
windows (#10), eliminated the exception unwinding (#11, ~27%), and made the unused
gradient lazy (#1). So the remaining leverage is **algorithmic**: the strong
atomic contractors IBEX ships and dReal doesn't run — above all **ACID** (which
IBEX's own solver enables by default). That is the headline.

> **Soundness framing (mandatory, per project `CLAUDE.md`):** every item here is a
> **COMPLETENESS** lever (tighter contraction → fewer search nodes / more
> refutations — `asserts φ^δ T-satisfiable on a T-unsatisfiable φ` is what looser
> contraction risks; a missed refutation, never a false `unsat`). None of these
> change soundness: a contractor that contracts *less* can only cost completeness.
> The soundness-load-bearing parts (gaol rounding, underflow) are the fork's job,
> already done (#8,#9,#10,#12). So these are safe to try — the worst case is "no
> speedup," not "wrong answer." SAT↔UNSAT *verdict* flips on benchmarks would
> signal an **integration bug**, not an expected outcome → escalate per `/benchmark`.

## Consolidated ranking (try-order)

| Try # | Item | Tier | Ease (1=hard,5=easy) | Likelihood it helps | Value if it does | Validate with |
|---|---|---|---|---|---|---|
| **1** | **`CtcAcid` shaving on the HC4 path** | A | 2 | **high** (IBEX's default) | **high** | `/benchmark` odeexpr (43 NRA) + ODE families; watch PAR2, no verdict flips |
| 2 | `Ctc3BCid` (fixed) + `s3b` sweep | A | 3 | high | high | OFAT sweep of `s3b`∈{5,10,20,50}; feeds #1's adaptivity sanity-check |
| 3 | Cross-check dReal's own fixpoint stop-ratio vs IBEX (0.01/0.1) | B | 4 | low–med | low–med | A/B the worklist ratio; likely small (micro-opt spent) |
| 4 | Fork-patch **integration guard** checklist | C | 5 | n/a (correctness) | safety | unit tests: callback fires, `is_empty()` path, lemma precision |
| 5 | `CtcNewton` as a *late* (small-box) contractor | D | 3 | low–med | med (near solutions) | enable with small `ceil`; odeexpr; mind gradient cost (#1) |
| 6 | **Revive polytope hull** (`LP_LIB`→soplex/clp, `--polytope` on) + tune X-Taylor | D | 2 | unknown | med | rebuild IBEX w/ LP; benchmark; then sweep `corners`/`slope` |
| 7 | `LinearizerAffine2` (build `ibex-affine` plugin) | D | 1 | unknown | med | requires plugin integration first |

## Tier A — strong contraction add-ons (the headline)

### A1. `CtcAcid` — adaptive 3BCID shaving on top of HC4  ⭐
- **What it buys:** ACID shaves variable bounds and constructive-disjoins the
  remainder, *adaptively* choosing how many variables to shave per box. It is the
  single strongest general-purpose contractor IBEX ships.
- **Hard evidence (the strongest single argument in this audit):** IBEX's own
  `DefaultSolver` composes, in order, `CtcHC4(sys, 0.01)` **then**
  `CtcAcid(sys, CtcHC4(sys, 0.1))` (`ibex-fork/src/solver/ibex_DefaultSolver.cpp:82-85`,
  verified). dReal composes **HC4 fwd-bwd only** (`src/dreal/solver/theory_solver.cc:197`).
  So dReal runs exactly the first half of IBEX's default contractor stack and omits
  the second — ACID is not an exotic add-on, it's the piece IBEX considers standard.
  Full cross-side analysis: [ARCHITECTURE-COMPARISON.md](ARCHITECTURE-COMPARISON.md)
  (which confirms this is the *only* loop-level "leftover" — see its verdict).
- **dReal status:** ⚪ not run. Node: [`classes/contractors/CtcAcid.md`](classes/contractors/CtcAcid.md).
- **How:** in `generic_contractor_generator.cc`, after building the per-constraint
  HC4 contractors, assemble an `ibex::System` over the Box variables (dReal already
  does this for the dormant polytope path — reuse it) and wrap the HC4 contractor
  in `CtcAcid(sys, hc4_ctc)`. Knobs: `ct_ratio=0.002`, `s3b=10` (see KNOBS §3).
- **Cost/risk:** per-box cost rises (ACID calls HC4 many times); net win depends on
  search-node reduction outweighing it. The HC4 call is already cheap (#1,#11).
  The sub-contractor must be dReal's **callback-bearing** fwd-bwd (not stock
  `CtcFwdBwd`) so theory lemmas survive — see C1.
- **Validate:** `/benchmark` odeexpr first (pure NRA, isolates the contractor),
  then ODE families. PAR2 < baseline = win; any SAT↔UNSAT flip = integration bug.

### A2. `Ctc3BCid` — the fixed-parameter sibling
- **What:** same shaving without ACID's adaptivity; you set `s3b`/`scid`/`vhandled`
  directly. Node: [`Ctc3BCid.md`](classes/contractors/Ctc3BCid.md).
- **Why bother if A1 exists:** it's the clean experiment to (a) confirm shaving
  helps dReal's instances at all and (b) find a good `s3b` (the header's
  "tune-first" param, best 5–200) before trusting ACID's auto-tuning. Cheaper to
  reason about; a good first probe.

## Tier B — cheap tuning of the path dReal already uses
### B1. Fixpoint stop-ratio cross-check
dReal's hand-rolled worklist (`contractor_worklist_fixpoint.cc`) has the analog of
`CtcPropag`'s ratio (IBEX 0.01) / `CtcFixPoint`'s (0.1). Worth confirming dReal's
value is in the same regime and A/B-ing it. **Expected small** — the docs warn the
ratio gives no guarantee on fixpoint distance, and the hot-path budget is spent.
KNOBS §2.

## Tier C — guards (correctness, not speed)
### C1. Fork-patch integration checklist for any borrowed contractor
Before A1/A2/D ship, confirm the borrowed IBEX contractor:
1. uses dReal's **callback-bearing** fwd-bwd as sub-contractor (lemma tracking,
   fork #2/#5/#6/#7) — not a fresh `CtcFwdBwd`;
2. detects emptiness via `is_empty()` (return-status, fork #11) — not a caught
   `EmptyBoxException`;
3. runs under `UpwardRoundingScope` so gaol rounding stays sound (#8,#9,#10,#12);
4. tolerates running inside DPLL(T) on transient literals (no global state that
   leaks across a throw — see the `Bug002` SIGBUS class in dReal's own tests).
This is a **conscious checklist**, not a code change — it's where an integration
silently degrades lemmas or soundness if skipped.

## Tier D — conditional / niche
- **D1. `CtcNewton` late.** Interval-Newton on small, square, solution-isolating
  subboxes (gate `ceil` small). Reintroduces the gradient build fork #1 made cold —
  so only worth it where convergence-phase tightening pays. Many ODE/`forall_t`
  queries aren't square. Node: [`CtcNewton.md`](classes/contractors/CtcNewton.md).
- **D2. Revive the polytope hull.** Two coupled decisions: flip `LP_LIB` to
  Soplex/CLP (re-adding a dep the team dropped) **and** turn on `--polytope`; then
  the unexplored X-Taylor knobs (`corners`, `slope` — KNOBS §4) become tunable.
  Uncertain payoff on dReal's instances; medium effort (build change). Nodes:
  [`CtcPolytopeHull.md`](classes/contractors/CtcPolytopeHull.md),
  [`LinearizerXTaylor.md`](classes/linear/LinearizerXTaylor.md).
- **D3. Affine linearization (`LinearizerAffine2`).** Tighter relaxation than
  X-Taylor for some systems — but **not in the fork** (it's the `ibex-affine`
  plugin). Build-integration task gated on D2 being worth it first.
- **D4. `CtcInverse` / `CtcQInter`.** Inverse-image contraction and outlier-robust
  q-intersection — niche; no obvious dReal use shape.

## Tier E — negative results (confirm dReal correctly ignores)
Recorded so future sessions don't re-investigate. dReal is an **SMT solver**, not a
standalone CSP/NLP solver; these serve a different problem shape:
- IBEX `Solver`/`Optimizer` ([solver](chapters/solver.md), [optim](chapters/optim.md))
  — dReal has DPLL(T) + nlopt.
- Separators / `Set` paving ([separator](chapters/separator.md), [set](chapters/set.md))
  — set *characterization*, not refutation (but see capability extensions).
- Minibex parser ([minibex](chapters/minibex.md)) — dReal has SMT2 + `.dr` parsers
  (dReal3 compat is intentional).
- Bisectors / cell buffers ([strategy](chapters/strategy.md)) — dReal branches
  inside DPLL(T).
- Inner arithmetic (`]f[`, `ibwd_*`) — for inner-region/inflation, not refutation.

## Capability extensions (non-perf future directions)
Not speed levers — new *capabilities* IBEX could give dReal, the IBEX analog of
the CAPD PDE/DAE notes:
- **Feasible-set output via separators + pavings.** If dReal wants to *return the
  set* of solutions to an ∃∀/parametric query (not just sat/unsat), IBEX's
  `Sep*` + `Set`/`SetInterval` machinery is the path
  ([separator](chapters/separator.md), [set](chapters/set.md)).
- **Rigorous optimization-modulo-theories** via the IBEX `Optimizer` +
  `ExtendedSystem` + `CtcKuhnTucker` — a *sound* global bound where dReal currently
  uses nlopt's local one ([optim](chapters/optim.md)).

## Honesty boundary (what's proven vs hypothesized)
**Proven** (header/doc/code-verified): every default and option menu in
[KNOBS.md](KNOBS.md); that ACID is IBEX's default contractor; that the polytope
path is dormant (`--polytope` off + `LP_LIB=none`); that affine isn't in the fork;
that the HC4 hot-path levers are already pulled (fork patches, with their measured
%); the Tier-E "different problem shape" reasoning.
**Hypothesized** (NOT yet measured — no benchmark was run for this audit): every
*expected speedup*. The ranking orders items by plausibility + ease, not by
evidence. Next step for any item is its "Validate with" column — start with A1 on
the odeexpr family.
