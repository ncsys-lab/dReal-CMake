# Soundness bug: an ODE constraint missing from `used_constraints_` yields an invalid theory lemma

**Status:** FIXED (2026-06-29). The Lohner contractor's inconclusive exits now record the ODE via
`ContractorStatus::AddInconclusiveOde`, and `GenerateExplanation` splices it into the explanation as
a non-expanding leaf keyed on the emptying witness. See §5.
**Class:** SOUNDNESS — asserts φ T-unsatisfiable on a T-satisfiable φ (false `unsat`).
**Discovered:** 2026-06-28, during a `--constraint-order` sweep.
**Root cause corrected:** 2026-06-29 — the earlier "variable-connectivity closure drops a
disconnected relational constraint" framing was the *symptom*, not the cause. See §3 and §9.

---

## 1. TL;DR

`dreal4 --constraint-order desc` *had* returned a **deterministic false `unsat`** on a δ-satisfiable
hybrid-automaton BMC instance. The proximate cause was a single **learned theory clause `¬E` whose
`E` is genuinely δ-satisfiable**. `E` was satisfiable because it was built entirely from the
conflict's **relational** constraints — the **ODE `integral` constraint that actually makes the
conflict infeasible was absent from `used_constraints_`**, so it never reached the explanation. The
ODE constraint was absent because, on this stiff flow, the CAPD integrator **diverges** and the
Lohner contractor returned early at the `if (!res.found)` exit **without recording the constraint**.
`--constraint-order desc` only changed which conflicts were hit; the defect was in explanation,
latent for all orders.

The explanation's variable-connectivity closure (`GenerateExplanation`,
`contractor_status.cc`) was **not** the root cause: it faithfully closes over whatever is in
`used_constraints_`, and `used` was missing the ODE. Returning the *full* `used_constraints_`
(no closure) does **not** fix it either (§4.3) — the full used set is also ODE-free and hence also
δ-satisfiable.

**Fix (§5):** the contractor now records the inconclusive ODE via `AddInconclusiveOde`, and
`GenerateExplanation` splices it into the explanation as a non-expanding leaf keyed on the emptying
witness — so the lemma includes the responsible ODE without dragging in the rest of the BMC ODE web.

---

## 2. Reproduction

Instance: a tacas C2E2 inverter BMC (hybrid automaton: `mode_*` discrete locations, per-step
`integral`/`forall_t` ODE dynamics over `Vm`, `Vout`, `stim`, `telapsed`, `timestep`, `t`).
Copies in-repo: `docs/artifacts/constraint_order_false_unsat.smt2` (k=12, original) and
`docs/artifacts/constraint_order_false_unsat_k10.smt2` (k=10 — same flip, **faster**: desc
short-circuits to the false `unsat` in <1 s; the unit tests drive this one).

```
gcc_build/dreal4 --precision 0.001                       <inst>   # -> delta-sat   (correct)
gcc_build/dreal4 --precision 0.001 --constraint-order asc <inst>  # -> delta-sat   (correct)
gcc_build/dreal4 --precision 0.001 --constraint-order desc <inst> # -> unsat       (FALSE)
```

`delta-sat` is a *witnessed* verdict (a δ-model accepted by the order-independent `EvaluateBox`),
so the model genuinely exists and desc's `unsat` is the false one. Across github+tacas+saradc
(119 instances) desc was the only config to flip a verdict, and it was net-worse (PAR2 1.20×).

### 2.1 Clause-level proof of invalidity

`docs/artifacts/invalid_learned_clause.smt2` is `box ∧ E` for one clause `¬E` that a desc run
learned (harvested via the auditor). A valid `¬E` requires `box ∧ E` to be `unsat`; this file is
`delta-sat`, and its δ-model is the instance's real model. So `¬E` is an invalid global clause that
excludes the real solution.

---

## 3. Root cause

A theory `CheckSat` receives a literal conjunction `A` (the SAT assignment's theory part — including
the `integral`/`forall_t` ODE literals) and a box `R`. ICP proves `R ∧ A` δ-unsat by a bisection
proof tree. The learned clause is `¬E` for `E ⊆ A` accumulated in `used_constraints_` — the
constraints a contractor recorded via `AddUsedConstraint` while pruning. For `¬E` to be sound, `E`
must be δ-unsat over `R` as a standalone formula.

**The defect:** the ODE `integral` constraints that make the conflict infeasible are **never added
to `used_constraints_`**, so they are not in `E`. Two code facts combine:

1. **The ODE formula-evaluator is a no-op.** `OdeFormulaEvaluator::operator()`
   (`ode_formula_evaluator.cc:37`) unconditionally returns `VALID`. So `EvaluateBox` never refutes
   via an ODE constraint and never records one. *All* ODE refutation/narrowing must come from the
   Lohner **contractor**.

2. **The Lohner contractor records the ODE only on paths that don't fire here.**
   `contractor_ode_lohner::Prune` calls `AddUsedConstraint(ic)` only when it refutes (no surviving
   terminal slice), narrows (`changed`), or hits a degenerate early case (param-intersect /
   `time=0` / trivial-flow). On this **stiff c2e2 flow CAPD diverges** (`run_capd_*` returns
   `found=false`), and (pre-fix) the contractor returned at the `if (!res.found)` exit in
   `contractor_ode_lohner::Prune` — **no narrowing, no recording**. Across a desc run: **121 / 145**
   ODE Prune calls diverge, **0 refute, 0 narrow** (none order: **16794 / 16794** diverge).

So the ODE is logically responsible for the conflict but invisible to the explanation. The
relational constraints (chaining `t_i_t = t_i_0 + time_i`, the `timestep = 50000·time` bridge, the
mode/guard literals) *are* recorded — and they are δ-satisfiable on their own (the candidate model
sits at the boundary `time_i = 2e-5`, which the ODE dynamics reject but the relational system
allows). Hence `E` is δ-satisfiable and `¬E` is invalid.

**One-line statement:** *a stiff ODE constraint that is logically responsible for a conflict is
dropped from `used_constraints_` (CAPD diverges → the Lohner contractor returns without recording),
so the learned theory clause is built from the satisfiable relational remainder and excludes a real
model.*

---

## 4. Evidence trail (call 803, the poisoning lemma)

Per-call dumps of `(E, used, A, box)` over the full desc run (820 unsat theory calls), each
re-checked with the committed solver. All instrumentation was temporary; the tree is clean.

### 4.1 Exactly one invalid base lemma
Re-checking `box ∧ E` for all 820: **819 are unsat (valid); call 803 is delta-sat (invalid).** It
is learned late and poisons the search — once `¬E_803` is in CaDiCaL it blocks the model assignment
and the loop returns false `unsat`.

### 4.2 The conflict is real; the integral is the reason
`box ∧ A(803)` solved standalone (with the real ODE flows) is **unsat** under both orders — so call
803's theory verdict is *correct*. Decomposing `A`:
- `box ∧ (A − forall_t)` = **unsat** → the invariant is not needed.
- `box ∧ (A − integral)` = **delta-sat** → the **`integral` constraints are the necessary reason.**

### 4.3 The reason is absent from `used` — and full-used doesn't help
`A` carries 55 `integral` + 44 `forall_t` constructs; **`used` carries zero** (it has the
`Vm`/`Vout` cross-step *chaining* equalities, but not the per-step dynamics). Consequences:
- `root ∧ used` = **delta-sat** → the *full* used set is also δ-satisfiable, so **returning full
  `used_constraints_` is also unsound here** (it too lacks the ODE), not merely slow.
- `root ∧ (used + all relational A\used)` = **delta-sat**; `root ∧ (relational A only)` =
  **delta-sat** → no relational subset refutes. The gap is the ODE, not any relational constraint.

### 4.4 The path is the CAPD-divergence early-return — confirmed by fixing it
Forcing the `if (!res.found)` divergence exit to record the integral (`AddUsedConstraint(ic)` +
`m_ctr.second`), **with pruning otherwise byte-identical**, flips desc from `unsat` to the correct
**delta-sat**. Since recording touches only `used`/explanations and not the search, the verdict
change isolates the cause to that non-recording exit. (This is a *diagnostic*, not the fix — see
§5: it makes `none` exceed 120 s, was 12 s.)

### Refuted / superseded hypotheses (do not re-chase)
- **The variable-connectivity closure drops a *disconnected relational* constraint.** Superseded:
  the dropped reason is the ODE, which is absent from `used` entirely; the closure is downstream.
- **"ODE not involved; used was 100 % relational" (earlier §4.1).** The zero-ODE observation was
  right; the conclusion was wrong — zero-ODE *is* the bug.
- **Strict/inclusive boundary (boxes are inclusive-only).** The empties are genuine: e.g.
  `timestep_7_t == timestep_7_0 + 50000·time_7` with `time_7=[0,0]` forces `timestep_7_t=0` vs the
  `≥1` guard. Not a `nextafter`/strict artifact.
- **`FilterAssertion` absorbs a bound without recording it.** The right *shape* of question, wrong
  subsystem: the unrecorded constraint is the ODE (Lohner contractor), not a `FilterAssertion`d
  relational bound (relational `A` is fully sat).
- **Box-conditioning on the recorded box fixes it.** Insufficient on its own (the box for call 803
  equals root; the missing piece is the ODE literal, not a box bound).

---

## 5. Fix (implemented 2026-06-29)

Two pieces, both in `src/dreal/contractor/`:

1. **Record the ODE at the inconclusive exits.** `contractor_ode_lohner::Prune` calls
   `cs->AddInconclusiveOde(ic)` at each non-recording early return — CAPD divergence
   (`if (!res.found)`), no-cache, unsupported-time, `win_ub<=0`. `AddInconclusiveOde` stores the
   `integral` literal in a separate `inconclusive_odes_` set on `ContractorStatus` (merged in
   `InplaceJoin` for parallel ICP). It does **not** seed the unsat witness — divergence does not
   empty the box.
2. **Splice it into the explanation as a non-expanding leaf, keyed on the witness.**
   `GenerateExplanation` runs the existing relational closure unchanged, then adds each
   `inconclusive_odes_` member whose variables intersect the **emptying `unsat_witness`** (the vars
   of the constraints that pruned the box to empty) — *not* the broad relational closure `seen`, and
   without growing `seen` (so the densely-chained BMC ODE web is never transitively pulled in).

**Why the witness, not the closure.** The responsible ODE shares the *time/state* var of the
emptying relational constraint (the relational system pins the candidate the ODE rejects — for call
803, the `time_i`/`timestep_i` pinch), so the witness is the principled "minimal relevant" tie. The
`integral` literals **are theory literals in `A`** (unlike ICP bisection splits), so the resulting
`¬(E ∧ relevant-ODE)` is an expressible, sound clause that refutes the assignment.

**Soundness is monotone**, so the splice can never make a sound `¬E` unsound (adding constraints only
shrinks the solution set of `R ∧ E`); the witness key is therefore a *performance* choice validated,
not assumed, by the oracles below.

### Rejected alternatives (measured)
- **Splice on the relational closure `seen` instead of the witness.** Sound and fixes the bug, but
  on densely-chained c2e2 conflicts it drags in ODEs from unrelated parts of the unrolling, bloating
  lemmas and starving the SAT search — a c2e2 k12 SAT instance went **9 s → timeout**. The witness
  key eliminates this (corpus A/B 1.01× PAR2, no instance lost).
- **Record every ODE on every divergence (into `used_constraints_`).** *Proven sound but
  catastrophic:* the connectivity closure pulls the whole chain into every lemma; `none` goes
  12 s → >120 s.
- **Return full `used_constraints_`.** Unsound here (§4.3, `root ∧ used` is delta-sat) *and* slow.
- **Reject `--constraint-order desc`.** The defect is latent for all orders; gating desc removes only
  the known trigger.
- **Make the ODE formula-evaluator refute.** A per-`EvaluateBox` ODE solve is prohibitive; the
  contractor is the right place, the gap was its *recording*.

### Validation
- **Splice unit tests** (`test/dreal/solver/test/constraint_order_soundness_test.cc`, microseconds —
  exercise `ContractorStatus::Explanation()` directly): a witness-touching inconclusive ODE reaches
  the explanation (`RelevantOdeReachesExplanation`); a disjoint one does not (`IrrelevantOdeExcluded`);
  one reachable only through the closure `seen`, not the witness, is excluded
  (`SplicedOnWitnessNotClosure`). Mutation-checked: removing the splice reds the first, keying on
  `seen` reds the third.
- **End-to-end test** (`DivergingOdeNoFalseUnsatEndToEnd`, ~3 s, **inline SMT2 string** — no
  external file): a 5-step C2E2-automaton BMC with the flows reduced to a stiff scalar ODE
  `v'=1e6*v^2` (diverges everywhere, like the real inverter). `desc` returned a false `unsat`
  pre-fix; the test asserts all orders now agree on the witnessed delta-sat. RED on the pre-fix
  solver. The false-`unsat` is an emergent property of the multi-step automaton (modes + transitions
  + invariants + intrinsic stiffness) — an extensive search for a ~10-line toy did not reproduce it;
  swapping the inverter ODE for any *non*-intrinsically-diverging flow (e.g. a plain blowup) also
  kills the flip, confirming the structure (not just a diverging ODE) is what triggers it.
- **Auditor oracle:** 2819 learned clauses re-checked across c2e2 k10 (desc), both flipping
  thermostats, and the c2e2 k12 ramp — **zero** with a satisfiable `box ∧ E`.
- **Corpus A/B** (HEAD vs fix, github+tacas+saradc sample): **1.01× PAR2, no verdict flips, no
  instance lost.**

---

## 6. Corpus context

`--constraint-order` swept on github+tacas+saradc (119 jobs), ref = `none`:

| config | verdicts | PAR2 mean | note |
|---|---|---|---|
| `none` | 116/119 | 65.2 s (1.00×) | baseline |
| `asc`  | 116/119 | 65.3 s (1.00×) | no-op (identical verdicts) |
| `desc` | 115/119 | 78.1 s (1.20×) | 1 **false-`unsat`** flip + 3 new TIMs |

The flag's stated hypothesis (ordering shortens lemmas) was null at the median; its lasting value is
surfacing this bug.

---

## 7. Honest open tensions
- **How desc's call 803 refutes `box ∧ A` while CAPD diverges throughout it.** `box ∧ A` is
  genuinely unsat (the integral rejects the boundary candidate `time_i = 2e-5`); the standalone
  solve proves it via a search where CAPD converges somewhere. In desc's call 803, CAPD diverges on
  every ODE Prune, so the integral never narrows; the relational system pinches the candidate to a
  measure-zero boundary that bisection refutes leaf-by-leaf. The §4.4 fix demonstrates the integral
  *is* responsible (recording it restores soundness), but the precise bisection-level account of the
  refutation is not fully reconstructed — it is a δ-completeness/search matter orthogonal to the
  (settled) explanation-soundness defect.
- **Minimal-relevant-ODE selection (resolved).** The witness-keyed splice (§5) is the cheap
  over-approximation that works: it captures the responsible ODE (auditor-clean over 2819 lemmas)
  without the broad-closure lemma bloat, at 1.01× corpus PAR2. The sharper delete-based minimization
  (`box ∧ (A − integral)` per conflict) was not needed.

---

## 10. The exact smell, and why it does not reduce to a small toy

Reading the harvested `box ∧ E` (`invalid_learned_clause.smt2`, the call-803 lemma) pins the
category precisely. **`E` is exactly the per-step forced clock-period skeleton** — for every step
`i`: `timestep_i_0 == 0` (reset), `timestep_i_t == timestep_i_0 + 50000·time_i` (clock rate), the
guard `timestep_i_t ≥ 1`, the continuity chaining (`x_i_t == x_i_0 + time_i`, `x_i_t == x_{i+1}_0`
for `t`/`stim`/`telapsed`), and the algebraic bound guards (`t<2`, `stim<1.2`, `stim>0`). The box
pins `timestep_t ∈ [1, 1.000005]`, so each `time_i` is pinned to the clock boundary `[2e-5, 2e-5+ε]`.
**There is no `mode` literal, no `v`, and no `integral` in `E`.** Every BMC step *is* one clock
period, so this skeleton is satisfied by the real model — the auditor's δ-model of `box ∧ E` *is*
that model. The genuinely-conflicting reason (the stiff `integral` over a full period) was dropped
when CAPD diverged, leaving only the model-satisfied skeleton. Negating a model-satisfied `E`
excludes the model → false `unsat`. This is the deterministic characterization the earlier "emergent"
framing lacked.

**The category, stated as a checklist.** All four must co-occur:
1. an **entailed/model-satisfied relational skeleton** the explanation can collapse onto (here the
   forced clock period — a constraint every trajectory obeys);
2. a stiff `integral` whose CAPD divergence is **state-dependent** (`v'=1e6·v²` integrates fine for
   small `v`, diverges only near the blow-up boundary), so it is the *sole* reason for a *specific*
   conflict yet is dropped from `used_constraints_`;
3. boolean **mode alternatives** so a real model exists *off* the conflicting assignment while sharing
   the skeleton `E` with it;
4. a search **driven into** the diverging-stiff conflict (the discrete transition guards + `forall_t`
   invariants under `desc` ordering) — the model does not need that step, but the search must *explore*
   and *learn* from it before finding the model.

**Why no hand-built toy reproduces it (evidenced, not assumed).** Ablation on the flipping k5
(against the pre-fix binary) shows all three structural pieces are load-bearing and *entangled with
satisfiability*: zeroing the clock rate (`timestep'→0`) makes it genuinely `unsat` (guard never met);
zeroing the stiffness (`v'→0`) makes it genuinely `unsat` (the `v>1.32` transition guard is
unreachable); stripping every `forall_t` keeps it δ-sat but **kills the flip** (the invariants are not
in `E` — §4.2 — yet they steer the search into the poisoning conflict). From-scratch 2-mode automata
with the entailed clock skeleton + state-dependent stiff divergence (stiff `v'=1e6·v²` mode + benign
`v'=0` mode, forced clock, growth target) stay δ-sat under both orders: a *free benign escape* lets
the solver reach the goal through the **feasible**
small-`v` stiff steps, so it never explores a diverging large-`v` step and never learns the poison.
Forcing exploration of the diverging region (pushing `v` past blow-up) instead removes the model and
yields a *genuine* `unsat`. Conditions 1–3 are easy to build; **condition 4 — a search that explores
a diverging conflict the model does not need — is what the multi-mode/multi-step inverter supplies and
a clean toy does not.** Hence the smallest faithful end-to-end reproducer remains the k5 automaton
(the inline test); the surgical splice logic itself is covered minimally by the unit tests.

---

## 8. Artifacts & regeneration
- `docs/artifacts/constraint_order_false_unsat{,_k10}.smt2` — the original flipping instances (§2;
  k12 + k10) used for the call-803 evidence trail.
- The end-to-end test's repro is an **inline SMT2 string** in
  `test/dreal/solver/test/constraint_order_soundness_test.cc` (the k=5 C2E2 automaton with a stiff
  `v'=1e6*v^2` flow), not a checked-in file.
- `docs/artifacts/invalid_learned_clause.smt2` — the proven-invalid `box ∧ E` (§2.1), harvested via
  the auditor. Regenerate: set `DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED true` (`src/dreal/version.h`),
  run desc, re-check the dumped `/tmp/dreal_audit/lemma*.smt2` for any `delta-sat`.
- Tests: `test/dreal/solver/test/constraint_order_soundness_test.cc` — microsecond unit tests of the
  splice plus the ~1 s k5 end-to-end flip (see §5 Validation).
- Diagnosis recipe (the call-803 dissection): instrument `theory_solver.cc` to expose the
  `ContractorStatus::UsedConstraints()` behind each explanation and `context_impl.cc` to dump
  `(E, used, A, box)` per unsat theory call; re-check `box ∧ E` / `box ∧ used` / `box ∧ A`
  standalone (the last needs the instance's `define-ode` preamble). Confirm the path with the §4.4
  one-line record-on-divergence at the `if (!res.found)` exit in `contractor_ode_lohner::Prune`.

---

## 9. Why the root cause was corrected

The first pass (2026-06-28) saw a small δ-satisfiable `E`, observed the closure had dropped
variable-disconnected members, and concluded the connectivity closure was unsound. That is a real
property of the closure but not the cause here: the genuinely-responsible constraint (the ODE
`integral`) was never in `used_constraints_` to begin with, so *no* explanation derived from `used`
— closure or full — could be sound. The corrected diagnosis (2026-06-29) traced the missing
constraint to the Lohner contractor's CAPD-divergence early-return and validated it by making that
path record (desc flips to delta-sat). The connectivity closure remains a separate, latent concern
for genuinely tree-refuted conflicts, but it is not what produces this false `unsat`.
