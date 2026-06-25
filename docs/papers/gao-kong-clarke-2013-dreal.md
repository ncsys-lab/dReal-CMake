# dReal: An SMT Solver for Nonlinear Theories over the Reals

**File:** `papers/gao-kong-clarke-2013-dreal.pdf`
**Authors:** Sicun Gao, Soonho Kong, Edmund M. Clarke (Carnegie Mellon University)
**Venue:** CADE-24 (24th International Conference on Automated Deduction), 2013; M.P. Bonacina
(Ed.), **LNAI 7898**, pp. 208–214, Springer (verified from the publisher PDF footer).

> Lineage position: theory ([gao-avigad-clarke-2012-delta-decidability](gao-avigad-clarke-2012-delta-decidability.md)) $\to$ procedure
> ([gao-avigad-clarke-2012-delta-complete](gao-avigad-clarke-2012-delta-complete.md)) $\to$ **tool** (this paper) $\to$ $\exists\forall$
> extension ([kong-solar-lezama-gao-2018-exists-forall](kong-solar-lezama-gao-2018-exists-forall.md)). This is the original **dReal tool**
> paper — the direct ancestor of the binary this repo builds (`gcc_build/dreal4`).

## Core Claim

Describes **dReal**, the open-source SMT solver implementing the $\delta$-complete decision
procedures of [gao-avigad-clarke-2012-delta-complete](gao-avigad-clarke-2012-delta-complete.md). On an input formula it returns
**`unsat`** or **`delta-sat`**, where $\delta$ is a user-specified numerical error bound, and it can
emit **certificates** for both answers (a witnessing solution for `delta-sat`, a checkable proof
tree for `unsat`). It handles polynomials plus transcendental functions (sin, tan, arcsin,
arctan, exp, log, pow, sinh) and, in principle, solutions of ODEs.

## Key Results

- **Input** (§2.1): SMT-LIB 2.0 with extensions; variable bounds declared as simple atomic
  formulas (e.g. `(assert (<= x 64.0))`); precision via `(set-info :precision 0.0001)` or the
  command line; **default precision $10^{-3}$**.
- **CLI** (§2.2): `dReal [--verbose] [--proof] [--precision <double>] <filename>`.
- **Certificates via `--proof`** (§2.2, §3.2): produces `filename.proof`. For `delta-sat`, a
  witnessing solution plugged into the $\delta$-perturbed formula (externally checkable). For
  `unsat`, a **proof tree** in a simple first-order natural-deduction system, verified with
  interval arithmetic by a separate `proofcheck` tool (default timeout 30 min $\to$
  "proof verified" / "timeout"). Details in CMU-CS-13-104.
- **DPLL(ICP) design** (§3.2, Alg. 1 "Theory Solving in DPLL(ICP)"): prune-as-`assert`
  (incomplete check, contract only) + prune-and-branch (complete check, look for a small box).
  **Backtracking and learning**: constraints appearing along a conflict are collected into a
  **learned clause** added to the formula.
- **Results** (§4): on Flyspeck (Kepler-conjecture) nonlinear benchmarks, **828 of 916** solved
  (`unsat`) at $\delta = 10^{-3}$ with a 5-minute timeout, no domain-specific heuristics; Table 1
  reports proof-checking depth and subproblem counts.

## Systems / Functions This Paper Motivates

| Paper concept | Where it lives in this project |
|---|---|
| The whole DPLL(ICP) tool pipeline | `docs/architecture.md` (Input $\to$ Context $\to$ preprocessing $\to$ SAT/Theory $\to$ contractors) |
| Theory solver: prune-as-assert + prune-and-branch + learned clause | `src/dreal/solver/theory_solver.cc` (`explanation_` $\to$ learned clause to the SAT layer) |
| `unsat` proof tree, externally re-checkable | `src/dreal/solver/auditor.cc` — reprints learned lemmas in dReal3-compatible format for independent re-checking (the descendant of the `--proof` / `proofcheck` workflow) |
| SMT-LIB 2.0 input + bounds-as-atoms + `:precision` | `docs/syntax-reference.md`; `src/dreal/smt2/` parser/driver |
| `--precision` (default $10^{-3}$) | `--precision <delta>` flag; `CLAUDE.md` §"Running the Solver" |
| Transcendental function support | `docs/syntax-reference.md` (trig/exp/log operators) |

## Open Proof Targets

- Take a current `unsat` from `gcc_build/dreal4`, reprint via `auditor.cc`, and re-check it
  independently — the modern analogue of the paper's `proofcheck` "proof verified," confirming
  the certificate story survived the backend swap.
- Re-run a few Flyspeck-style instances on the current binary and compare to the paper's
  828/916 @ $\delta=10^{-3}$ as a coarse regression of the $\delta$-complete guarantee (not
  timing — backends differ).

## Notes

- **Misalignment (backend drift) — the headline flag.** This paper states dReal is "built on
  **opensmt** [5] for the high-level DPLL(T) framework, and **realpaver** [14] for the ICP
  algorithm." **None of that is current.** dReal4 (this repo) uses **CaDiCaL** for Boolean CDCL,
  the **IBEX** fork for ICP / HC4 contractors, and **CAPD** as the sole ODE backend
  (`CLAUDE.md` §Architecture; `docs/decisions.md` "ODE backend"). The **DPLL(ICP) framework and
  the $\delta$-sat/`unsat` contract are unchanged**; only the components are replaced.
- **Misalignment (certificates).** The `--proof` / `proofcheck` external-checker workflow
  described here is the dReal3-era design. In dReal4 the analogous role is `auditor.cc`
  (dReal3-format lemma reprinting); a standalone `proofcheck` binary with the 30-min default is
  not part of the core loop. dReal3 `.dr` backward-compatibility is intentionally retained
  (`CLAUDE.md` §"Key Design Notes").
- **Misalignment (ODEs).** This paper only gestures at ODE support ("can be added when needed").
  The mature ODE story (QF_NRA_ODE, `Integral`/`ForallT`, CAPD per-slice tube) is later work —
  see `docs/ode-integration.md`, `docs/qf_nra_ode_semantics.md`.
