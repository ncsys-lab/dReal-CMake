# `forall` Semantics in dReal4

> **⚠ PITFALL — `forall` ≠ `forall_t` (`forall-vs-forall_t`).** Independent machinery that
> only share a prefix: **`forall`** is the ∃∀ NRA quantifier (this document); **`forall_t`**
> is the ODE trajectory invariant (`qf_nra_ode_semantics.md` §5). Full side-by-side in §7.
> Never substitute one for the other.

This document covers the standard SMT2-LIB `forall` quantifier (`Formula::Forall`,
`Kind::FORALL`, handled by `ContractorForall`) — **not** the ODE-specific `forall_t`.

Every claim is cross-referenced to the parser (`src/dreal/smt2/parser.yy`, `scanner.ll`),
the CNFizer (`src/dreal/util/tseitin_cnfizer.cc`), the contractor
(`src/dreal/contractor/contractor_forall.h`), the formula evaluator
(`src/dreal/solver/forall_formula_evaluator.cc`), and the theory solver
(`src/dreal/solver/theory_solver.cc`).

---

## 1. Supported Fragment: ∃∀ Only

dReal supports the **∃∀ alternation** in `QF_NRA`. The shape of every satisfiable query is:

```
∃ x₁,...,xₙ.  ∀ y₁,...,yₘ.  φ(x, y)
```

- **Existential variables** (`x`): declared at top level with `declare-fun`.
  Conceptually bound by the implicit `∃` of the SMT2 `check-sat` command.
- **Universally quantified variables** (`y`): declared inside the `forall` binder.
  Their domain must be specified with `[lb, ub]` bounds (see §2.2).

**Depth-one only.** A second quantifier alternation — `∀∃`, `∃∀∃`, nested `∀∀` — is not
supported and will crash at contractor construction time (see §6).

The logic is declared as:
```smt2
(set-logic QF_NRA)
```
There is no dedicated `NRA` or `NRA_ALL` tag. The `forall` token is recognized regardless of
the declared logic.

---

## 2. Syntax

### 2.1 Grammar

From `parser.yy:381`:

```
term:
    '(' TK_FORALL enter_scope '(' variable_sort_list ')' term exit_scope ')'
    {
        const Variables& vars   = $5.first;
        const Formula& domain   = $5.second;
        const Formula body      =
            Smt2Driver::EliminateBooleanVariables(vars, $7.formula());
        const Variables qvars   = intersect(vars, body.GetFreeVariables());
        if (qvars.empty())
            $$ = body;
        else
            $$ = forall(qvars, imply(domain, body));
    }
```

Example:

```smt2
(set-logic QF_NRA)
(declare-fun x () Real [0, 8])
(assert
  (forall ((y Real [0, 7.5]))
    (or (<= (+ (^ (- x 2) 2) (^ (- y 2) 2)) 9)
        (<= (+ (^ (- x 5) 2) (^ (- y 5) 2)) 9))))
(check-sat)
```

Multiple universally-quantified variables are listed in the same binder:

```smt2
(forall ((x2 Real [-32.0, 32.0]) (y2 Real [-32.0, 32.0]))
  (<= z (+ (* x2 x2) (* y2 y2))))
```

### 2.2 Bounds encoding

`variable_sort_list` (`parser.yy:593`) converts each `(y Real [lb, ub])` declaration into:
- A new `Variable` registered in the current scope
- A domain formula `(lb ≤ y) ∧ (y ≤ ub)` accumulated into `domain`

At the `forall` production, the body is wrapped as `domain ⟹ body` via `imply(domain, body)`.
Variables declared without bounds get `[-∞, +∞]` and the domain contribution drops to `true`.

**Practical requirement**: always supply bounds on forall variables. Without bounds the CE
search ranges over (-∞, +∞). If a solution with no counterexamples can be found quickly
(e.g., `x = 0` trivially satisfies `∀y. x·y ≤ 1`), the solver may still terminate. But
for problems where every candidate `x` has a counterexample at large `|y|`, ICP bisection
on the unbounded forall domain will not terminate.

### 2.3 Boolean variable elimination

`EliminateBooleanVariables(vars, body)` case-splits any Boolean variables in `vars` out of the
body before constructing the `forall` node. This is a parse-time rewrite, not a solver step.

### 2.4 Dead-variable pruning

If after `EliminateBooleanVariables` none of the declared variables actually appear free in
the body, the `forall` node is dropped and the body formula is returned directly
(`if (qvars.empty()) $$ = body`). The `forall` is a no-op when the body does not depend on
the quantified variables.

---

## 3. CNF Distribution

The Tseitin CNFizer (`tseitin_cnfizer.cc:126`) distributes `∀` over CNF clauses:

```
∀y. φ(x, y)
    = ∀y. (clause₁(x,y) ∧ ... ∧ clauseₙ(x,y))    [φ CNF-ized by NaiveCnfizer]
    = (∀y. clause₁(x,y)) ∧ ... ∧ (∀y. clauseₙ(x,y))
```

Clauses that do not mention any `y` variable are emitted without the quantifier. If there is
more than one quantified clause, Tseitin introduces a fresh Boolean variable `forallN` as their
conjunction representative and records the relationship in `map_`.

**Effect on the body structure**: each `ContractorForall` sees a single clause as its
quantified formula, not the original conjunction. Multiple forall constraints map to
multiple `ContractorForall` instances in the fixpoint loop.

---

## 4. Contractor Algorithm and δ-Completeness

> **Paper map.** This CE-guided loop is the algorithm of Kong, Solar-Lezama & Gao,
> *Delta-Decision Procedures for Exists-Forall Problems over the Reals* (CAV 2018 —
> `papers/kong-solar-lezama-gao-2018-exists-forall.md`). The three δ levels in §4.1 are that
> paper's **double-sided error control** (§3.2): the chain `inner_delta < epsilon < delta`
> instantiates the paper's `δ' < ε < δ`, which exists to reject *spurious counterexamples*. The
> `--local-optimization` refiner (§6.7) is its §3.3 locally-optimized counterexamples.

`ContractorForall<ContextType>` (`contractor_forall.h`) implements a
**counterexample-guided (CE) pruning loop**:

```
Problem:
  Given box B (over existential vars x) and formula ∀y∈D. φ(x, y),
  reduce B to B' ⊆ B.

Algorithm:
  repeat
    1. Fix x ← B. Solve strengthen(¬φ, ε) ∪ {y ∈ D}  at precision inner_δ.
       (= find a y in D that violates φ for some x in B)
    2. If no CE found: B is consistent with ∀y∈D.φ(x,y). Break.
    3. If CE (a,b) found:
         Instantiate: φ(x, b)   [y fixed at midpoints of CE box]
         Contract:    B' ← Contract(φ(x,b), B)
         If B' = ∅: set B = ∅, break.
         B ← B'; continue.
  until fixed-point
```

### 4.1 Three δ levels — and two regimes

The CE search is governed by three precisions in the strict order
`inner_delta < epsilon < delta`, enforced by `DREAL_ASSERT` (the `ContractorForall` ctor
`@pre`, `contractor_forall.h:75, 86–89`). They map onto the CAV 2018 paper's
`δ' < ε < δ` — the **double-sided error control** (§3.2) — exactly:

| Paper symbol | Code name | Role |
|---|---|---|
| `δ` (outer precision) | `config.precision()` / `delta` | the user's `--precision`; the band the final answer is relaxed by |
| `ε` (CE strengthening) | `epsilon` | amount `¬φ` is strengthened by, so a found CE is *genuinely* violating (not a boundary artifact) |
| `δ'` (CE-search precision) | `inner_delta` | precision of the nested δ-decision that searches for a CE |

**Two regimes for the same constraint.** The `forall` constraint is handled by two
components that pick *different* values inside this ordering:

| Component | `epsilon` | `inner_delta` | Site |
|---|---|---|---|
| **`ContractorForall`** (prunes the box) | `delta * 0.5` | `epsilon * 0.5` (= `delta/4`) | `theory_solver.cc:188–190` |
| **`ForallFormulaEvaluator`** (branching oracle, §5) | `0.99 * delta` | `0.99 * epsilon` (= `0.9801·delta`) | `theory_solver.cc:287–289` |

The evaluator's `(0.99δ, 0.9801δ)` is the CAV 2018 experimental choice verbatim (§5:
`ε = 0.99δ`, `δ' = 0.98δ`); the contractor is deliberately more conservative
(`δ/2, δ/4`). Both satisfy `δ' < ε < δ`, so both are δ-complete instantiations (§4.5) —
the paper proves completeness for *any* point in that open region, not a specific one.
(The doc-comment at `contractor_forall.h:57` writes "`Solve(strengthen(¬φ, ε), δ) where
ε > δ`"; the `δ` there is the *inner-solve* precision `δ'`, so it reads `ε > δ'` —
consistent with the ordering, the symbol is just overloaded.)

The CE-search context is built once at contractor construction
(`context_for_counterexample_`) with `precision = inner_delta` and `strengthen(¬φ,
epsilon)` asserted (`contractor_forall.h:82–83, 96–118`). On each `Prune` it re-sets the
existential intervals and calls `CheckSat`.

### 4.2 Stacking alternation

`Prune` alternates `stack_left_box_first` on the CE-search context on each call:
```cpp
config_for_counterexample.mutable_stack_left_box_first() =
    !config_for_counterexample.stack_left_box_first();
```
This diversifies the search direction of the nested ICP between contractor invocations.

### 4.3 CE instantiation detail

When a CE box is found, the forall variables are pinned to the **midpoints** of their CE
intervals (`safe_mid(counterexample[forall_var], ur)`) and the existential-variable ranges from
the CE box are replaced with the current outer box values. This produces a point-instantiated
constraint `φ(x, b_mid)` that the inner `contractor_` (over existential variables only) uses
to narrow `B`.

### 4.4 Multithreaded variant

`ContractorForallMt` (`contractor_forall.h:261`) wraps `ContractorForall` in a
thread-local allocation pattern, creating one `ContractorForall` instance per thread.
The inner CE-search context forces `number_of_jobs = 1` regardless of the outer parallelism
setting.

### 4.5 δ-Completeness guarantee

The CE-guided procedure is **δ-complete** for the ∃∀ (`CNF^∀`) fragment: for any
`δ ∈ ℚ⁺` it terminates and returns either `unsat` (φ is unsatisfiable) or `delta-sat`
(the δ-weakening φ⁻ᵟ is satisfiable). This is Theorem 1 of CAV 2018 (p. 229), which
chains two results:

1. **The ∀-clause pruning operator is well-defined** (CAV 2018 Lemma 1): it satisfies the
   three conditions of a sound ICP pruning operator — contraction, never discards a real
   solution, non-empty results are faithful — from the IJCAR 2012 paper (W1–W3,
   `papers/gao-avigad-clarke-2012-delta-complete.md`).
2. **A branch-and-prune procedure with well-defined operators is δ-complete** (IJCAR 2012
   Thm 4.2 / Cor 4.1).

So `forall` inherits δ-completeness from the *same* theorem that covers the purely
existential `QF_NRA` contractor; the ∀-clause is "just another" well-defined pruning
operator, parameterized over the quantified variables. The guarantee covers both
components of §4.1 (the `ContractorForall` and the `ForallFormulaEvaluator`), since each
uses a valid `δ' < ε < δ` instantiation.

### 4.6 Why the three levels are mandatory: the spurious-counterexample hazard

The strengthening `ε` is not a tuning knob — it is what makes the procedure complete. The
CE search is itself a δ-decision (it can only certify `φ⁻ᵟ'`, never φ exactly), so without
strengthening it can return a **spurious counterexample**: a point `b` with
`⋀ᵢ fᵢ(a, b) ≤ δ'` instead of the genuine `< 0` (CAV 2018 §3.2). A spurious CE does not
actually violate the clause, so `Contract(φ(x, b), B)` prunes nothing — the fixpoint loop
(`Prune`, `contractor_forall.h:196–235`) stalls after one iteration. The outer
branch-and-prune is then left to terminate on **branching alone**, and can report
`delta-sat` with a box `‖B‖ ≤ δ` that contains *no* δ-solution (a missed refutation).

The fix (CAV 2018 §3.2, Eqs 2–3) is to strengthen the CE query by `ε` and solve it at
`δ'`, with **`δ' < ε`** (so a returned CE clears the strengthening and is genuine) and
**`ε < δ`** (so the strengthening stays inside the user's band). That is precisely the
`inner_delta < epsilon < delta` invariant of §4.1, and the reason for
`DeltaStrengthen(¬φ, epsilon)` at `contractor_forall.h:82–83`.

### 4.7 Soundness vs. completeness of `forall` failure modes

Every way the `forall` machinery can go wrong is a **completeness** failure, never a
**soundness** one — consistent with the project-wide principle that over-approximating
contractors cannot produce a false `unsat` (`docs/soundness-vs-completeness.md`):

| Failure | Effect | Class |
|---|---|---|
| Spurious CE accepted (no `ε`-strengthening) | fixpoint stalls → `delta-sat` on a box with no δ-solution | **COMPLETENESS** (asserts φ⁻ᵟ T-satisfiable on a T-unsatisfiable φ⁻ᵟ — missed refutation) |
| Inner CE search misses a real CE (under-powered nested solve) | an invalid candidate `x` survives → `delta-sat` on an `x` that has a counterexample | **COMPLETENESS** (missed refutation) |
| CE search times out / over-branches | premature `delta-sat` with a small box | **COMPLETENESS** |

The reason no failure mode is a soundness hazard: the pruning step contracts `B` with
`φ(x, b_mid)` for a **real** point `b_mid` in the forall domain (§4.3). Since
`∀y. φ(x, y)` *requires* `φ(x, b_mid)`, removing the `x` that violate it can never delete
a true ∃∀-solution — it is a sound over-approximation no matter how the CE was found. A
bad CE only ever costs progress (refutation power), i.e. completeness. The double-sided
error control of §4.6 is therefore a **completeness safeguard**, not a soundness one.

---

## 5. Formula Evaluator

`ForallFormulaEvaluator` (`forall_formula_evaluator.cc`) is the ICP branching oracle. It runs
the same CE search as the contractor but returns a `FormulaEvaluationResult`:

- **CE found** → `UNSAT` with a diameter indicating how badly the constraint is violated
  (used by ICP to pick bisection candidates)
- **No CE** → `VALID`

The evaluator holds one `Context` per thread-job and dispatches via `thread_local
kThreadId` (`forall_formula_evaluator.cc:68–73`). Its CE search is *structurally* the same
as the contractor's — assert `DeltaStrengthen(¬φ, epsilon)`, set the existential intervals,
`CheckSat` (`forall_formula_evaluator.cc:84, 96, 101–107`) — but it runs at a **different
precision regime**: `epsilon = 0.99·delta`, `inner = 0.9801·delta`
(`theory_solver.cc:287–289`), the CAV 2018 experimental values, *not* the contractor's
`δ/2, δ/4` (§4.1). It is not "initialized identically" to the contractor — only the CE
query structure matches, not the precisions.

---

## 6. Limitations and Caveats

### 6.1 Nested `forall` crashes

If the body of a `forall` contains another `forall`, the solver throws at contractor
construction time. The root cause is `DeltaStrengthen` (`symbolic.cc:399–401`):

```cpp
Formula VisitForall(const Formula&, const double) const {
    throw DREAL_RUNTIME_ERROR(
        "DeltaStrengthenVisitor: forall formula is not supported.");
}
```

`DeltaStrengthen(!body, epsilon)` is called on the negated body during `ContractorForall`
construction. If the body contains a `forall` node, this throws immediately. There is no
graceful degradation — the solver aborts.

**Implication for nested-quantifier projects**: any ∃∀∃ or ∀∃∀ formula requires
transformation before it can be fed to dReal (see §8).

### 6.2 No quantifier elimination

dReal does not implement quantifier elimination (QE). There is no Cylindrical Algebraic
Decomposition (CAD) or virtual term substitution path. The CE-guided loop is a complete
procedure for the ∃∀ fragment over bounded domains (in the delta-completeness sense), but it
produces a delta-SAT witness for `x`, not a QE certificate. Returning an explicit description
of the set of `x` satisfying `∀y.φ(x,y)` is not supported.

### 6.3 Single quantifier alternation

Only `∃∀` is supported — one block of existentials, one block of universals. The standard
SMT2 `∀x.∃y.φ` direction (validity checking) is not handled; feeding it to dReal will either
produce incorrect results or crash. Multiple alternation levels (`∃∀∃`, `∀∃∀`) are not
supported.

### 6.4 Bounded forall domain required in practice

Although the grammar allows unbounded quantified variables (`(y Real)` without `[lb, ub]`),
the CE search will not terminate over an unbounded domain for a nonlinear body. Always bound
forall variables.

### 6.5 Body structure after CNF distribution

The body passed to each `ContractorForall` is a single clause (from Tseitin distribution). A
body that is a conjunction of atoms produces multiple `ContractorForall` instances (one per
conjunct after distribution). The body passed to the CE solver is the strengthened negation
of that single clause, i.e., a conjunction of strengthened literals. If the original body was
already a single clause (disjunction), the CE search looks for a point violating all disjuncts
simultaneously.

### 6.6 `DeltaStrengthen` on strict vs. non-strict inequalities

`DeltaStrengthen(f, ε)` tightens `e ≥ 0` to `e ≥ ε` and `e > 0` to `e > ε`. After negation,
`¬(e ≥ 0)` → `e < 0` → strengthened to `e < -ε` → `e ≤ -ε - ε'`. This ensures the CE is a
genuine δ-violating point and not a boundary artifact. The strengthening amount is
`epsilon = delta/2`.

### 6.7 Performance

Each `ContractorForall::Prune` call runs a **full nested dReal solve**. For nonlinear bodies
over multi-variable forall domains, the inner solve is itself an ICP loop with contractors,
branching, and SAT calls. The outer ICP's learned facts about the existential box are not
available to the inner solve (each `Prune` only sets the existential intervals, not any
derived constraints). Expect roughly 10–100× overhead per forall constraint compared to a
purely existential formula of the same complexity.

**`--local-optimization`** (`dreal_main.cc:152`): enables `CounterexampleRefiner`
(`counterexample_refiner.{h,cc}`), which sharpens each CE before pruning by running a
**local optimization** that pushes the CE to *further* violate the clause — it reframes the
violated atom `e₁ ◦ e₂` as an objective (`minimize e₁ − e₂`) plus the clause atoms as
constraints (`counterexample_refiner.cc:77–104`). A sharper CE prunes more per iteration,
cutting the number of fixpoint iterations (CAV 2018 §3.3, Fig. 1). The optimizer is
**NLopt**: `LD_SLSQP` (Sequential Least-Squares QP) when the query is differentiable, else
`LN_COBYLA` (Constrained Optimization BY Linear Approximations)
(`counterexample_refiner.cc:68–75`) — the exact pair the CAV 2018 implementation used (§5;
its experiments set `1e-6` tolerances, a `1e-3 s` timeout, and 100 max evaluations, and
report speed-ups on 20 of 23 global-optimization instances, up to ~250×). When the query
has no exist∧forall coupling the refiner is a no-op and returns the raw CE (`opt_` stays
null, `counterexample_refiner.cc:59–63`).

**`--polytope`** / `(set-option :polytope true)`: routes the CE-search inner context
through IBEX polytope (LP-based) contractors, which can prune the forall domain more
aggressively. Controlled by `use_polytope_in_forall` (`contractor_forall.h:98`). This is
the linear-programming pruning the CAV 2018 implementation ran on **CLP** (§5). **Caveat**:
it needs an LP solver linked into the build, and `cmake-build-release` (this repo) does not
link one — `--polytope` on problems whose linear assertions trigger the LP path crashes with
`LPSolver method called but no LPSolver has been configured`. Simple forall problems with no
top-level linear assertions may work; the Ackley-family tests fail with it. (The current
source build links **SoPlex** — `CMakeLists.txt` `-DLP_LIB=soplex` — so the polytope path is
live; this caveat is the historical `LP_LIB=none` state.)

**`--forall-pre-prune`** / `(set-option :forall-pre-prune true)` (default off): runs a sound
`ibex::CtcForAll` **proj-intersection pre-pruner** (`ContractorIbexForall`) *beside* the CEGIS
decider in the forall fixpoint — pure interval contraction on the existential box, no nested
δ-solve. It augments, never replaces, the δ-complete `ContractorForall`. `--forall-pre-prune-prec`
(default 0.5) sets the universal-box bisection precision. COMPLETENESS-only (cannot move a
verdict). NRA ∃∀ only; runs under `--jobs>1` via a per-worker `ContractorIbexForallMt` cell.
The mechanism and its IBEX-lever rationale are in
`ibex_docs/AUDIT-QUANTIFIERS.md` Q1; the measured tractability outcome on odeexpr_v2 (sound,
but speedup is encoding-fragile and never cracks the pinned δ) is in
`docs/exists_forall_perf.md`.

---

## 7. Relationship to `forall_t` — the canonical disambiguation (`forall-vs-forall_t`)

> This table is the **canonical** `forall` vs `forall_t` reference; the other docs and code
> anchors (grep `forall-vs-forall_t`) point here. They are entirely separate code paths — a
> confusion that has bitten this repo before (a contractor was mislabeled as the other). When
> in doubt, check the AST node and the contractor: `Formula::Forall` / `ContractorForall`
> (`Kind::FORALL`) is the NRA quantifier; `FormulaKind::ForallT` / `contractor_ode_lohner`
> (`Kind::ODE_LOHNER`) is the ODE invariant.

| Aspect | `forall` | `forall_t` |
|--------|----------|------------|
| Purpose | ∃∀ quantified NRA | ODE trajectory invariant |
| Token | `TK_FORALL` | `TK_FORALLT` |
| AST node | `Formula::Forall` | `FormulaKind::ForallT` |
| Quantified over | real-valued SMT variables | ODE time parameter |
| Bounds notation | `(y Real [lb, ub])` in variable list | `[lb ub]` as a time interval |
| Contractor | `ContractorForall` | `contractor_ode_lohner` invariant check |
| Requires linking | no | yes — must match an `integral` formula |
| `NaiveCnfizer` | recursively CNFizes body | passes through unchanged |
| `DeltaStrengthen` | throws if nested inside another body | passes through unchanged |
| Logic tag | `QF_NRA` | `QF_NRA_ODE` |

---

## 8. Nested-Quantifier Projects: What Is and Is Not Possible

For a planned project making heavy use of nested quantifiers, the relevant capabilities and
hard limits are:

**Supported (∃∀)**:
- Finding a parameter `x` such that a constraint `φ(x, y)` holds for all `y` in a bounded domain.
- Lyapunov certificate synthesis: `∃V. ∀x∈D. (V(x) ≥ 0) ∧ (∇V·f(x) ≤ 0)`.
- Minimax / robust optimization: `∃x. ∀y∈D. f(x) ≤ g(y)` (combined with a point-value assertion on `x`).

**Not supported without transformation**:
- `∀x.∃y.φ(x,y)` — validity checking direction. Would require negation + ∃∀ re-encoding.
- `∃x.∀y.∃z.φ(x,y,z)` — the innermost ∃ over `z` cannot be expressed.
- Quantifier elimination results (explicit semi-algebraic description of the satisfying set).
- Counting or optimization over the full satisfying set.

**Workaround for deeper alternation**: quantifier alternations beyond ∃∀ must be handled by
external preprocessing — e.g., skolemization to reduce `∃∀∃` to `∃∀` at the cost of
introducing Skolem function terms, or iterative CEGIS loops that call dReal as a subroutine
for the inner ∃∀ check.

### 8.1 Lyapunov synthesis — the canonical ∃∀ application

The headline application in CAV 2018 (§5.2) is synthesizing a Lyapunov function for a
dynamical system `ẋ = f(x)`, `x ∈ X`, in a **single ∃∀ solve** — *searching for* the
certificate and *verifying* it at once, unlike verify-only pipelines (e.g. Kapinski et al.,
HSCC 2014, where dReal only checked a simulation-guided candidate). Fix a polynomial
template `v(x) = zᵀ P z` (`z` a monomial vector over `x`, `P` a symmetric coefficient
matrix); the unknowns are the entries of `P`. The synthesis query asserts the standard
Lyapunov conditions — positive definite and non-increasing along trajectories:

```
∃P. [ v(x) = zᵀ P z
      ∧ ∀x ∈ X∖{0}. v(x) > 0
      ∧ ∀x ∈ X.      ∇v(x)ᵀ · f(x) ≤ 0 ]
```

A `delta-sat` witness is a δ-valid Lyapunov function. Worked result (CAV 2018, normalized
pendulum `ẋ₁ = x₂`, `ẋ₂ = −sin(x₁) − x₂`, quadratic template, `‖x‖ ∈ [0.1, 1.0]`,
`δ = 0.05`): the solver returns `v = 40.6843·x₁x₂ + 35.6870·x₁² + 84.3906·x₂²` in ~44 s.
A time-varying system (the damped **Mathieu** equation) is handled identically by adding
the time `t` as an extra forall variable.

This is the ∃∀ shape a control-synthesis project would build on. Note the two universal
clauses become **two separate `ContractorForall` instances** after CNF distribution (§3),
each running its own CE loop; and the `∀x ∈ X∖{0}` domain (a punctured set) must be encoded
explicitly (e.g. `0.01 ≤ ‖x‖²`), since the contractor only takes box `[lb, ub]` domains
(§2.2).

---

## 9. Example: Global Minimum via ∃∀

The pattern: assert that `z` is a lower bound for `f` over the forall domain AND that `z`
equals `f` at a specific point. `delta-sat` witness is a δ-global minimizer.

```smt2
; find x ∈ [-2,2] minimizing x² (trivial: min = 0 at x = 0)
(set-logic QF_NRA)
(declare-fun x () Real [-2, 2])
(declare-fun z () Real)
(assert (forall ((y Real [-2, 2])) (<= z (* y y))))  ; z ≤ y² for all y in [-2,2]
(assert (= z (* x x)))                               ; z = x²
(check-sat)
; → delta-sat  x ≈ 0, z ≈ 0
```

For more complex objectives (transcendental functions, multiple variables), the same
two-assertion pattern applies. See `test/dreal/test/smt2/exist_forall_06.smt2` (Ackley3)
and `exist_forall_07.smt2` (Ackley4) for multi-variable examples — both return `delta-sat`
without `--polytope`. Do not add `--polytope` to transcendental benchmarks unless LP is
compiled in (see §6.7).

The general pattern (CAV 2018 §5.1): a constrained global minimization `min f(x) s.t. φ(x)`
over `x ∈ ℝⁿ` encodes as the ∃∀ formula

```
φ(x) ∧ ∀y. (φ(y) → f(x) ≤ f(y))
```

— "x is feasible, and no feasible y beats it." Because this is a *guaranteed* global search
(not local descent), it solves highly non-convex objectives that defeat gradient methods:
CAV 2018 Table 1 reports 23 standard benchmarks at `δ = 1e-4`, including Ripple1 (252 004
local minima) driven to its global optimum. `--local-optimization` (§6.7) only sharpens the
*inner* CE search; the outer global guarantee is unaffected.

---

## 10. Grammar Summary

```
forall-term := '(' 'forall' '(' var-sort-list ')' body ')'
var-sort-list := (var-sort)*
var-sort      := '(' SYMBOL 'Real' ')'              ; unbounded
             |  '(' SYMBOL 'Real' '[' lb ',' ub ']' ')'  ; bounded

body := any SMT2 term that evaluates to a formula
        (including disjunctions, conjunctions, implications)
        WARNING: must not contain a nested 'forall'

Result AST: forall(qvars, imply(domain, body))
  where domain = ⋀ᵢ (lbᵢ ≤ yᵢ) ∧ (yᵢ ≤ ubᵢ)
```
