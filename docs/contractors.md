# Contractors

Contractors are the core computational primitive in the theory layer. A contractor `C` takes a box `B` (a set of variable domains) and returns a tighter box `C(B) ⊆ B` that is guaranteed to contain all solutions. If `C(B)` is empty, there are no solutions.

All contractors implement the same interface (`src/dreal/contractor/contractor.h`):

```cpp
void Contractor::Prune(ContractorStatus* cs, const UpwardRounding& ur) const;
```

`Prune` reads `cs->box()`, tightens it in-place, and updates the explanation set in `cs` to record which constraints were responsible for any pruning.

The `ur` parameter is a zero-size capability token (`src/dreal/util/rounding.h`). The gaol/IBEX interval backend is sound only with the FPU in `FE_UPWARD` rounding mode, so the whole ICP contraction phase runs under `FE_UPWARD`, established **once** per phase by an `UpwardRoundingScope` (in `IcpSeq::CheckSat` and each `IcpParallel` worker) rather than per `Prune`. `UpwardRoundingScope` is the only minter of an `UpwardRounding`, and it is required to call `Prune` — so "the rounding mode is established" is a compile-time obligation a contractor cannot bypass. The pure-gaol leaves (`ContractorIbexFwdbwd`, `ContractorIbexPolytope`) no longer guard the mode themselves; they `DREAL_ASSERT_ROUNDING(FE_UPWARD)` to verify the inherited phase mode in Debug builds. See CLAUDE.md "FPU rounding mode" for the full design.

---

## Contractor Kinds

Defined in `Contractor::Kind`:

| Kind | File | Description |
|---|---|---|
| `ID` | `contractor_id.cc` | Identity — no pruning, pass-through |
| `INTEGER` | `contractor_integer.cc` | Round integer-typed variables to integer bounds |
| `SEQ` | `contractor_seq.cc` | Apply a list of contractors in sequence |
| `IBEX_FWDBWD` | `contractor_ibex_fwdbwd.cc` | HC4 forward-backward propagation (main workhorse) |
| `IBEX_POLYTOPE` | `contractor_ibex_polytope.cc` | Linear relaxation (polytope) contractor |
| `FIXPOINT` | `contractor_fixpoint.cc` | Run a contractor to fixpoint |
| `WORKLIST_FIXPOINT` | `contractor_worklist_fixpoint.cc` | Fixpoint with dependency tracking |
| `FORALL` | `contractor_forall.h` | ForallT (universal quantification over time) |
| `JOIN` | `contractor_join.cc` | Disjunctive composition (convex hull of results) |
| `ODE_LOHNER` | `odes/contractor_odes.cc` | CAPD order-10 Taylor integration for ODEs |

---

## IBEX Forward-Backward (HC4)

**File:** `src/dreal/contractor/contractor_ibex_fwdbwd.cc`

This is the primary contractor for algebraic constraints. Given a formula `f(x₁,...,xₙ) ≤ 0` (or `= 0`, `≥ 0`), it:

1. **Forward pass**: Evaluates `f` bottom-up using interval arithmetic, computing an interval enclosure for each subexpression.
2. **Backward pass**: Propagates tighter bounds top-down by inverting each operator.

Example for `x * y ≤ 1` with `x ∈ [0.5, 2]`, `y ∈ [0.5, 3]`:
- Forward: `x * y ∈ [0.25, 6]`
- Backward: knowing the product must be ≤ 1 and `x ≥ 0.5`, we get `y ≤ 1/0.5 = 2`, so `y ∈ [0.5, 2]`

IBEX implements this as `ibex::CtcFwdBwd`. The dReal wrapper converts `dreal::Formula` → `ibex::NumConstraint` via `IbexConverter` (`src/dreal/util/ibex_converter.h`).

**Important soundness note**: Strict inequalities (`<`, `>`) require careful handling of the bound. The `FilterAssertion` function in `contractor_ibex_fwdbwd.cc` uses `nextafter()` to convert strict bounds to IBEX's closed-interval representation. A past soundness bug existed here — see the git log for the fix.

---

## IBEX Polytope

**File:** `src/dreal/contractor/contractor_ibex_polytope.cc`

Linearizes the constraint system and applies polytope (LP-based) contraction. More expensive than FWDBWD but can prune regions that interval arithmetic alone misses, especially for tightly coupled linear or near-linear constraints.

Used selectively — `TheorySolver` chooses whether to include a polytope contractor based on formula structure.

---

## Sequential Composition

**File:** `src/dreal/contractor/contractor_seq.cc`

`ContractorSeq` applies a list of contractors in order:

```
C_seq([C₁, C₂, ..., Cₙ]).Prune(B) = Cₙ(...C₂(C₁(B))...)
```

The result is tighter (or equal) to any individual contractor. Order matters because each contractor may prune domains that help subsequent contractors prune further.

---

## Fixpoint

**File:** `src/dreal/contractor/contractor_fixpoint.cc`

Runs a contractor repeatedly until the box stops shrinking:

```
C_fix(C, termination).Prune(B):
  loop:
    B' ← C.Prune(B)
    if termination(B, B'): break
    B ← B'
  return B'
```

The termination condition is a function `(old_box, new_box) → bool`. Typically it checks whether the relative improvement in box volume is below a threshold.

`ContractorWorklistFixpoint` is a smarter version that tracks which constraints depend on which variables, so it only re-runs a contractor when one of its input variables was tightened by a previous contraction.

---

## Join (Disjunctive Composition)

**File:** `src/dreal/contractor/contractor_join.cc`

For disjunctive formulas `φ₁ ∨ φ₂`, neither `C_{φ₁}` nor `C_{φ₂}` alone is sound (we can't prune based on one branch if the other might still be satisfiable). The join contractor:

```
C_join([C₁, C₂]).Prune(B) = hull(C₁(B), C₂(B))
```

returns the interval hull (smallest enclosing box) of both results. This is sound because any solution must be in at least one branch, hence in the hull.

---

## Forall Contractor

**File:** `src/dreal/contractor/contractor_forall.h`

Handles `ForallT` formulas: `∀t ∈ [t₀, t₁]: φ(x, t)`. These appear in ODE mode when checking that a property holds for all time points along a trajectory. The forall contractor samples or integrates over the time domain to prune the state space.

---

## ODE Contractor (`contractor_ode_lohner`)

**File:** `src/dreal/contractor/odes/contractor_odes.cc` (CAPD backend in `contractor_odes_capd.cc`)

See `docs/ode-integration.md` for a full description.

At the contractor interface level: given an ODE constraint and a time window, `contractor_ode_lohner::Prune` integrates with **CAPD** (order-10 `IOdeSolver` + `ITimeMap`) — the sole ODE backend since the Codac elimination. `run_capd_fwd` integrates `f(x)` forward from the initial box to narrow the terminal state, then integrates the negated `-f(x)` backward from the narrowed terminal to narrow the initial state, so a single call narrows both endpoints. A trivial-flow short-circuit (every RHS is the literal `0`) and a `T=0` short-circuit bypass CAPD entirely. On integration divergence the call narrows nothing for that `Prune` (sound but incomplete). The previous Codac / CAPD-gated hybrid (and the `--capd-t-gate` / `--capd-ndim-gate` flags) was retired — see `CODAC_MIGRATION.md`.

---

## Contractor Composition in TheorySolver

`TheorySolver::BuildContractor` constructs the composite contractor for a set of assertions:

1. For each asserted formula, create a `ContractorIbexFwdbwd` (or `contractor_ode_lohner` for ODE constraints).
2. Optionally create a `ContractorIbexPolytope` for the combined linear relaxation.
3. Wrap them all in `ContractorSeq`.
4. Wrap the sequence in `ContractorFixpoint` with a convergence termination condition.

The result is a single `Contractor` object that, when `Prune`d, repeatedly applies all constraints until convergence.

---

## Dependency Tracking

Every contractor maintains a `DynamicBitset input()` indicating which box dimensions it reads. `ContractorWorklistFixpoint` uses this to rebuild its worklist efficiently: if contractor `Cᵢ` prunes dimension `d`, only contractors with `d ∈ input()` need to be re-queued.

---

## Adding a New Contractor

1. Add a new `Kind` to `Contractor::Kind` in `contractor.h`.
2. Create `contractor_foo.h` / `contractor_foo.cc` implementing `ContractorCell`.
3. Add a factory function `make_contractor_foo(...)` and declare it as a `friend` of `Contractor`.
4. Implement `Prune(ContractorStatus* cs, const UpwardRounding& ur)`, `input()`, `include_forall()`, and `operator<<`. If the contractor wraps gaol/IBEX interval arithmetic, route the raw call through `ibex_hc4_backward` (`util/rounded_interval.h`) — passing `ur` — rather than calling `ibex::Function::backward` directly; forward `ur` to any child contractors' `Prune`. A contractor that needs `FE_TONEAREST` internally (like the CAPD ODE contractor) opens a `NearestRoundingScope` (and passes its `NearestRounding` token to nearest-regime consumers such as `run_capd_*`), then re-establishes a nested `UpwardRoundingScope` before invoking any gaol sub-contractor. The static lint `rounding_lint.py` enforces this routing.
5. Wire up construction in `TheorySolver::BuildContractor` or `context_impl.cc`.
