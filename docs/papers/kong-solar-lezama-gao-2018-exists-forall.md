# Delta-Decision Procedures for Exists-Forall Problems over the Reals

**File:** `papers/kong-solar-lezama-gao-2018-exists-forall.pdf`
**Authors:** Soonho Kong (Toyota Research Institute), Armando Solar-Lezama (MIT),
Sicun Gao (UC San Diego)
**Venue:** CAV 2018 (30th International Conference on Computer Aided Verification), LNCS 10982,
pp. 219–235, DOI 10.1007/978-3-319-96142-2_15.

> Lineage position: theory ([gao-avigad-clarke-2012-delta-decidability](gao-avigad-clarke-2012-delta-decidability.md)) $\to$ procedure
> ([gao-avigad-clarke-2012-delta-complete](gao-avigad-clarke-2012-delta-complete.md)) $\to$ tool ([gao-kong-clarke-2013-dreal](gao-kong-clarke-2013-dreal.md)) $\to$
> **$\exists\forall$ extension** (this paper). This is the algorithmic source for dReal's
> `forall` (`ContractorForall`) support.

## Core Claim

Extends $\delta$-complete decision procedures from the existential fragment to **$\exists\forall$
problems** — formulas with universally quantified real variables and arbitrary nonlinear
functions. The method integrates **CEGIS-style counterexample synthesis into the
branch-and-prune framework**: universally-quantified clauses become *pruning operators* that use
counterexamples (violating assignments of the $\forall$-variables) to contract the box over the
existential variables. The paper proves the resulting procedure is **$\delta$-complete** for the
whole $\exists\forall$ class, by carefully controlling the numerical errors that a naive CEGIS
over the reals would introduce.

## Key Results

- **$\mathrm{CNF}^\forall$ normal form** (Def. 2): $\varphi(\boldsymbol{x}) := \bigwedge_i
  \forall \boldsymbol{y} (\bigvee_j c_{ij}(\boldsymbol{x}, \boldsymbol{y}))$; universal
  quantifiers push inside conjuncts ($\forall$-clauses).
- **$\delta$-weakening / strengthening** (Def. 3): $\varphi^{-\delta}$ relaxes $f_{ij} \geq 0$ to
  $\geq -\delta$; $\varphi^{+\delta}$ tightens to $\geq +\delta$. $\delta$-completeness (Def. 4):
  `unsat` ($\varphi$ unsat) or `delta-sat` ($\varphi^{-\delta}$ sat).
- **$\forall$-clauses as pruning operators** (§3.1, Alg. 2). To prune the box $B_x$: query
  $\mathrm{Solve}(\boldsymbol{y}, \psi, \delta')$ for a counterexample $b$ that
  $\delta'$-satisfies $\psi = \bigwedge_i f_i(x,y) < 0$ (the negation of the clause); pin $y$ to
  $b$ and contract $B_x$ via ordinary pruning on each $f_i(x,b) \geq 0$; take the **box-hull**
  ($\bigsqcup$) over the clause's disjuncts. CEGIS-in-branch-and-prune, learning at the *model*
  level rather than the constraint level.
- **Double-sided error control** (§3.2) — the technical crux. A $\delta$-decision counterexample
  search can return a **spurious CE** satisfying $\bigwedge f_i(a,b) \leq \delta$ instead of
  $< 0$, which stalls the fixpoint. Fix: strengthen the CE query by $\varepsilon$
  ($\psi^{+\varepsilon} = \bigwedge f_i(a,b) \leq -\varepsilon$) and solve it at weakening
  $\delta'$, with the constraints $\delta' < \varepsilon$ (Eq. 2) and $\varepsilon < \delta$
  (Eq. 3). The chain $\delta' < \varepsilon < \delta$ is what makes counterexamples *genuine*.
- **Locally-optimized counterexamples** (§3.3): locally optimize the CE $b$ so it "further
  violates" the constraint $\Rightarrow$ stronger pruning, fewer fixpoint iterations (Fig. 1).
- **$\delta$-completeness** (§4, Lemma 1): the $\forall$-clause pruning operator of Alg. 2 is
  **well-defined** in the sense of [gao-avigad-clarke-2012-delta-complete](gao-avigad-clarke-2012-delta-complete.md) (Def. 5 / W1–W3), so
  the overall branch-and-prune procedure is $\delta$-complete.
- **Applications** (§5): global optimization and **Lyapunov function synthesis**.

## Systems / Functions This Paper Motivates

| Paper concept | Where it lives in this project |
|---|---|
| CE-guided $\forall$-clause pruning (Alg. 2) | `src/dreal/contractor/contractor_forall.h` — the `ContractorForall::Prune` CE loop; `docs/forall-semantics.md` §4 |
| Double-sided error control $\delta' < \varepsilon < \delta$ | The **three $\delta$ levels** in `theory_solver.cc:187–193`: `delta` (outer) $>$ `epsilon` $=\delta/2$ (strengthen $\neg\varphi$) $>$ `inner_delta` $=\varepsilon/2$ (CE-search precision); `forall-semantics.md` §4.1. **The code pins a specific instantiation of the paper's general $\delta' < \varepsilon < \delta$.** |
| CE query = $\delta$-decision on the strengthened negation | `forall_formula_evaluator.cc` (the ICP branching oracle runs the same CE search); §4.2/§6.6 `DeltaStrengthen(\neg\varphi, \varepsilon)` |
| Locally-optimized counterexamples (§3.3) | `--local-optimization` $\to$ `CounterexampleRefiner` (`forall-semantics.md` §6.7) |
| CE midpoint instantiation + box-hull over disjuncts | `forall-semantics.md` §4.3 (`safe_mid` pinning) |
| Lyapunov / global-optimization applications | `forall-semantics.md` §8 ($\exists V.\, \forall x \in D.\, \ldots$), §9 (global min via $\exists\forall$) |

## Open Proof Targets

- Verify the code's pinned $\varepsilon = \delta/2$, $\text{inner} = \varepsilon/2$ satisfies the
  paper's $\delta' < \varepsilon < \delta$ for the actual `Solve` weakening used — i.e. confirm
  the implementation's instantiation lands inside the proven-complete region.
- Reproduce a small Lyapunov-synthesis instance (§5) on `gcc_build/dreal4` and confirm
  `delta-sat` with a witness $V$; check the certificate against $\varphi^{-\delta}$.

## Notes

- **This is the algorithmic basis of dReal's `forall`.** The three-$\delta$-level structure that
  looks arbitrary in `forall-semantics.md` §4.1 is exactly the paper's double-sided error
  control; reading §3.2 here explains *why* `inner_delta < epsilon < delta` is mandatory (spurious
  counterexamples otherwise stall the fixpoint and yield an unsound-looking `delta-sat`).
- **Alignment, not drift, on quantifier depth.** The paper is **$\exists\forall$ (single
  alternation)** by construction ($\mathrm{CNF}^\forall$), matching dReal's depth-one support.
  Deeper alternation ($\exists\forall\exists$) is out of scope in *both* — it needs external
  transformation (`forall-semantics.md` §8).
- **Misalignment (implementation artifacts not in the paper).**
  - **Nested-`forall` crash:** feeding a `forall` whose body contains another `forall` throws at
    contractor construction (`DeltaStrengthen::VisitForall`, `symbolic.cc`;
    `forall-semantics.md` §6.1). This is an *un-handled-input* abort, not a paper limitation —
    the paper simply assumes $\mathrm{CNF}^\forall$ input.
  - **`--polytope` LP footgun:** the polytope CE-search path crashes when no LP solver is linked
    (`forall-semantics.md` §6.7) — a build-packaging detail, outside the paper.
  - **Pinned error parameters:** the paper proves completeness for any $\delta' < \varepsilon <
    \delta$; the code fixes $\varepsilon = \delta/2$, $\text{inner} = \varepsilon/2$. A valid
    instantiation, but a concrete choice the paper leaves open.
