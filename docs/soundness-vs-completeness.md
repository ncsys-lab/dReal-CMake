# Soundness vs. completeness in dReal (and the notation we report it in)

dReal4 is a **sound but δ-complete** decision procedure. Those two words are not
interchangeable, and confusing them has a concrete cost in this project: a
*completeness* trade-off (the hull-grid / "F1" finding) was reported as a
*soundness* issue, which read as a correctness emergency and got experiments
cancelled. This file is the canonical reference for the distinction and the
**reporting convention** `CLAUDE.md` enforces.

The one-sentence version:

> A **soundness** bug makes the solver say `unsat` about something that is
> actually satisfiable (a *false `unsat`*). A **completeness** bug makes the
> solver *fail to say `unsat`* about something it should have refuted (a *missed
> refutation / false `delta-sat`*). The first is forbidden; the second is, beyond
> the δ-band, a defect but a *different* one — and within the δ-band it is by
> design.

---

## The four T-relations (quantifier-explicit)

Let φ be a ground formula over a signature Σ and T the background theory (here:
nonlinear real arithmetic, with ODE/`Integral`/`ForallT` atoms). M ranges over
Σ-structures; "M ⊨ T" means M is a model of the theory. (Definitions match
`smt-model-theory/references/02-satisfaction-and-validity.md` — keep them in
sync; do not paraphrase-drift.)

| Relation | Quantifier form | Reading |
|---|---|---|
| φ is **T-satisfiable** | ∃M. M ⊨ T ∧ M ⊨ φ | some T-model satisfies φ |
| φ is **T-unsatisfiable** | ∀M. M ⊨ T ⇒ M ⊭ φ | every T-model falsifies φ (≡ ¬φ is T-valid) |
| φ is **T-valid** | ∀M. M ⊨ T ⇒ M ⊨ φ | every T-model satisfies φ (≡ ¬φ is T-unsatisfiable) |
| φ is **T-invalid** | ∃M. M ⊨ T ∧ M ⊭ φ | some T-model falsifies φ (≡ ¬φ is T-satisfiable) |

T-unsatisfiable and T-satisfiable are exact negations of each other; so are
T-valid and T-invalid. The SMT question dReal answers is T-satisfiability of φ.

---

## What dReal's two answers actually guarantee

dReal does not decide T-satisfiability exactly (it is undecidable for this T).
It implements the **δ-decision** framework — the δ-SMT decision procedure of Gao,
Avigad & Clarke (*δ-Complete Decision Procedures for Satisfiability over the Reals*,
**IJCAR 2012**; summary `papers/gao-avigad-clarke-2012-delta-complete.md`), resting on
their decidability/complexity theory (*Delta-Decidability over the Reals*, **LICS
2012**; `papers/gao-avigad-clarke-2012-delta-decidability.md`). These are two distinct
2012 papers — do not collapse them to one cite. Fix the precision δ > 0
(`--precision`). Write
φ^δ for the **δ-weakening** of φ (every numeric bound relaxed by δ); note
φ ⇒ φ^δ is T-valid, so φ T-satisfiable ⇒ φ^δ T-satisfiable, but not conversely.

The two verdicts and their guarantees:

- **`unsat`** ⟹ φ is **T-unsatisfiable** (∀M ⊨ T. M ⊭ φ). This is exact and
  hard. **This is the sound side.**
- **`delta-sat`** ⟹ **φ^δ is T-satisfiable** (∃M ⊨ T. M ⊨ φ^δ). It does *not*
  promise φ itself is T-satisfiable — only the δ-relaxation is. This is the
  relaxed side.

So the procedure's contract is: *if φ is T-unsatisfiable, return `unsat`; if φ^δ
is T-satisfiable, return `delta-sat`.* When φ is T-unsatisfiable but φ^δ is
T-satisfiable (the unsat gap is ≤ δ), **either answer is permitted** — that is
the designed δ-slack.

---

## The dichotomy: which bug is which

Almost every "the ODE/interval contractor did the wrong thing" report in this
repo is one of exactly two failures. Name them precisely:

| Failure | What the solver did | Model theory | Class | Severity |
|---|---|---|---|---|
| **false `unsat`** | returned `unsat` on φ that has a (δ-)model | asserts φ T-**un**satisfiable (∀M⊨T. M⊭φ) when actually φ is T-**satisfiable** (∃M⊨T. M⊨φ) | **SOUNDNESS** | forbidden — corrupts the only hard guarantee |
| **missed refutation / false `delta-sat`** | returned `delta-sat` on φ that is robustly (gap ≫ δ) infeasible | asserts φ^δ T-**satisfiable** (∃M⊨T. M⊨φ^δ) when actually φ^δ is T-**unsatisfiable** (∀M⊨T. M⊭φ^δ) | **COMPLETENESS** | a defect, but the *delta-complete* contract is what is weakened, not soundness |

The reasoning that fixes the class for **contractors** (interval/ODE narrowing):

- A contractor computes an **over-approximation** of the feasible set and prunes
  the box to it. An over-approximation **never removes a real solution**, so a
  *looser* enclosure can never produce a false `unsat` → **soundness is preserved
  no matter how loose the enclosure is.** (Emptying the box is sound iff the
  enclosure is genuinely disjoint from the gate, which an outward
  over-approximation guarantees.)
- What a looser enclosure *does* cost is **refutation power**: it may fail to
  empty a box that a tighter enclosure would have emptied → it fails to derive
  `unsat` → **completeness weakens.** Tightening the enclosure (higher Taylor
  order, finer hull grid, smaller tolerance) buys completeness, never soundness.

Corollary, stated as a slogan: **"too loose" is a completeness problem;
"wrongly narrowed" is a soundness problem.** A knob that only widens enclosures
(e.g. `--ode-hull-grid` down, `--ode-taylor-order` down) is a *completeness* knob
and can never be a soundness hazard. A bug that narrows a box past a real
solution (mis-rounded interval, scrambled model, wrong `nextafter` on a strict
bound, truncated ODE feed) is a *soundness* hazard.

### The flip side: where soundness genuinely lives

Soundness *is* at stake whenever code can **remove a point that a real model
occupies**. Recurring soundness sites in this repo (all genuine, all already
handled — see `CLAUDE.md` Key Design Notes): gaol interval arithmetic under the
wrong FPU rounding mode (inverts an interval → empties the box → false `unsat`);
the 6-digit `std::to_string` ODE-feed truncation (CAPD integrates the wrong
field → false `unsat`); subnormal/underflow in HC4 backward (#321); strict-bound
`nextafter` handling in `filter_assertion`. These are correctly called soundness
bugs because each can prune away a true solution.

---

## The reporting convention (enforced in CLAUDE.md)

When you report, label, or comment on a soundness/completeness issue — in chat,
in code comments, in commit messages, in docs — **append the model-theory
characterization in parentheses**, naming which T-relation the solver *asserts*
versus which is *true*. Do not write a bare "soundness bug" / "soundness gate".

Templates:

- Soundness: `SOUNDNESS (returns unsat / asserts φ T-unsatisfiable on a
  T-satisfiable φ — false unsat)`
- Completeness: `COMPLETENESS (returns delta-sat / asserts φ^δ T-satisfiable on a
  T-unsatisfiable φ — missed refutation)`

The parenthetical is not ceremony: writing out "asserts φ^δ T-satisfiable when
φ^δ is T-unsatisfiable" makes it impossible to file a missed-refutation under
"soundness," which is the exact mistake this discipline exists to prevent.

---

## Worked example: the hull-grid / F1 finding

Setup (full record: `HULL_COMPLETENESS.md`; verifier-integrity angle:
`~/.claude/rules/dont-game-the-verifier.md`). Lowering `--ode-hull-grid` makes
each per-slice ODE enclosure wider. The `FwdInteriorInvariantViolation` ("F1")
test has an invariant violated only at a trajectory's *interior* peak; at a
coarse hull grid the slice enclosure straddling the peak is too wide to clear the
constraint, so the box does **not** empty.

Classify it: lowering hull-grid only *widens* a sound outward over-approximation,
so it **cannot** produce a false `unsat`. The actual failure is the box failing
to empty on a robustly-infeasible (margin 0.2 ≫ δ) instance → the solver returns
`delta-sat` where `unsat` was provable.

> **COMPLETENESS (returns `delta-sat` / asserts φ^δ T-satisfiable on a
> T-unsatisfiable φ — missed refutation).** It is *not* a soundness issue: no
> false `unsat` is reachable by widening enclosures.

Reporting it as "HULL SOUNDNESS" was the error that read as a correctness
emergency. The honest report is a *completeness* defect — which, as a
delta-complete trade-off (hull-4 default, refutation-critical problems raise the
knob), is the owner's call, not a reason to halt.

---

## A second worked example: the ∃∀ double-sided error control

The `forall` (∃∀) contractor has a failure mode that is, again, *completeness* not
soundness — worth stating because it can look alarming. Its counterexample search is
itself a δ-decision, so without strengthening it can return a **spurious counterexample**:
a `y` that only δ-violates the clause instead of strictly violating it. A spurious CE
prunes nothing, the pruning fixpoint stalls, and the outer search can report `delta-sat`
on a tiny box that holds no δ-solution.

Classify it: the pruning step always contracts with `φ(x, y)` for a *real* `y` in the
domain, which `∀y.φ(x,y)` genuinely requires — so it can never delete a true solution (no
false `unsat` is reachable). The damage is a missed refutation.

> **COMPLETENESS (returns `delta-sat` / asserts φ⁻ᵟ T-satisfiable on a T-unsatisfiable
> φ⁻ᵟ — missed refutation).** The `inner_delta < epsilon < delta` "double-sided error
> control" (Kong, Solar-Lezama & Gao, CAV 2018, §3.2; `docs/forall-semantics.md` §4.6) is
> the *completeness* safeguard that rules it out — not a soundness mechanism.

---

## See also

- `smt-model-theory` skill — the model-theory reference for the publication work;
  `references/02-satisfaction-and-validity.md` is the source for the T-relation
  definitions above.
- `HULL_COMPLETENESS.md` — the hull-grid completeness coupling, in full.
- `docs/ode-integration.md` §Soundness — separates feed-faithfulness (soundness)
  from per-slice correlation (completeness) for the ODE contractor.
- `docs/decisions.md` "Denormal / underflow soundness" — a worked
  "delta-completeness imprecision, not a crisp soundness bug" call.
- `papers/gao-avigad-clarke-2012-delta-complete.md` — Gao, Avigad, Clarke, *δ-Complete
  Decision Procedures for Satisfiability over the Reals* (**IJCAR 2012**): the δ-SMT /
  δ-weakening / δ-completeness framework, and the well-defined-pruning conditions (W1–W3)
  that make "looser enclosure ⇒ completeness loss, never soundness loss" a theorem.
- `papers/gao-avigad-clarke-2012-delta-decidability.md` — Gao, Avigad, Clarke,
  *Delta-Decidability over the Reals* (**LICS 2012**): the underlying decidability +
  NP/PSPACE complexity theory (distinct paper from the IJCAR one above).
