# Papers — foundational literature for dReal4

Companion summaries for the papers that define the theory dReal implements. Each `.pdf` has a
sibling `.md` summary (Core Claim · Key Results · Systems/Functions it motivates · Open Proof
Targets · Notes, incl. **misalignments with the current code**). The PDFs predate the
continually-developed source, so every summary flags where paper and code diverge.

These four form a single lineage — **theory → procedure → tool → ∃∀ extension** — all by
Sicun Gao and collaborators (the original dReal developers):

| Paper (summary) | Citation | Role | Cross-referenced into |
|---|---|---|---|
| [Delta-Decidability over the Reals](gao-avigad-clarke-2012-delta-decidability.md) | Gao, Avigad, Clarke — LICS 2012, pp. 305–314 | **Theory**: δ-decision problem is decidable; NP/PSPACE complexity; boundedness+robustness necessary | `soundness-vs-completeness.md`, `smt-model-theory` skill |
| [δ-Complete Decision Procedures for Satisfiability over the Reals](gao-avigad-clarke-2012-delta-complete.md) | Gao, Avigad, Clarke — IJCAR 2012, LNAI 7364, pp. 286–300 | **Procedure**: δ-SMT, well-defined pruning (W1–W3), DPLL(ICP) is δ-complete | `soundness-vs-completeness.md`, `architecture.md`, `contractors.md`, `symbolic.cc` |
| [dReal: An SMT Solver for Nonlinear Theories over the Reals](gao-kong-clarke-2013-dreal.md) | Gao, Kong, Clarke — CADE-24 2013 (LNAI 7898, pp. 208–214) | **Tool**: the original dReal; certificates; Flyspeck results | `architecture.md`, `theory_solver.cc`, `auditor.cc`, `icp.h` |
| [Delta-Decision Procedures for Exists-Forall Problems over the Reals](kong-solar-lezama-gao-2018-exists-forall.md) | Kong, Solar-Lezama, Gao — CAV 2018, LNCS 10982, pp. 219–235 | **∃∀ extension**: CEGIS-in-branch-and-prune; double-sided error control | `forall-semantics.md`, `contractor_forall.h` |

## Headline misalignments flagged across these summaries

- **Backend drift** (2013 tool paper): "built on opensmt + realpaver" → dReal4 uses **CaDiCaL +
  IBEX + CAPD**. The DPLL(ICP) framework and the δ-sat/`unsat` contract are unchanged; the
  components are all replaced.
- **Citation fix** (both 2012 papers): the docs previously cited the IJCAR title *δ-Complete
  Decision Procedures* against the **LICS** venue — two distinct 2012 papers, now split correctly
  (theory = LICS, procedure = IJCAR).
- **∃∀ implementation artifacts** (2018 paper): nested-`forall` crash and the `--polytope`
  LP-not-linked crash are un-handled-input/packaging issues outside the paper; the pinned
  `ε = δ/2`, `inner = ε/2` is one valid instantiation of the paper's general `δ' < ε < δ`.

## Adding a paper

Drop the PDF in `unorganized/` and invoke `/lit-review` (Phase 2). The skill reads page 1 to
identify it, renames to `author-year-keyword.pdf`, and creates the `.md` from the template
below. **Source-fidelity:** every cited detail must be verified in the PDF, not recalled; math in
the `.md` uses pandoc inline `$...$` (these compile to PDF via the `reading-packet` skill, where
bare unicode math symbols are missing from the body font).

### Companion `.md` template

```markdown
# [Full Paper Title]

**File:** `papers/<filename>.pdf`
**Authors:** ...
**Venue:** ...

## Core Claim
One paragraph: what the paper proves/shows and why it matters here.

## Key Results
Bullet list of the key theorems, definitions, or empirical findings.

## Systems / Functions This Paper Motivates
Table mapping paper concepts → project code (factories, utils, contractors, docs).

## Open Proof Targets
Concrete proof obligations this paper suggests, phrased as dReal/SymPy tasks.

## Notes
Non-obvious caveats — especially **misalignments with the current code** (the PDFs are
older than the source).
```
