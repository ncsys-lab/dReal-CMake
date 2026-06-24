# Hull-grid soundness coupling — a latent looseness in the per-slice ODE tube

**Status:** open finding, fix deferred. Defaults reverted to the proven-good
order-20 / hull-16. This documents *why* hull-grid cannot be freely lowered, the
root cause, and the proposed fix, so the next session does not re-make the
mistake of "just pick a hull-grid that passes the test."

## TL;DR

The per-slice tube enclosures the ODE contractor produces are **far looser than
CAPD's actual precision allows**, because the sub-slicing uses a fixed *count*
(`kHullGrid`) per CAPD step. When CAPD takes a large adaptive step, each
sub-interval is wide, and evaluating the Taylor curve over a wide time-interval
(`curve(sub)`) blows up via the **polynomial dependency problem**. This silently
weakens refutation power — most visibly, **interior invariant-violation
detection** — and couples a *soundness-relevant* capability to a *performance*
knob. Lowering `--ode-hull-grid` for speed therefore trades away refutation
completeness, invisibly. The fix is to bound the sub-interval **width**, not its
count.

## How it surfaced

The 2026-06 meta-parameter sweep (OPTIMIZATION_LOG.md "2026-06 re-tuning
campaign") found that forward order 12 + hull-grid 4 was ~2× faster on the
123-job ODE corpus with **zero** SAT↔UNSAT flips, and that default was briefly
adopted. The Debug suite then failed exactly one test:

```
[ FAILED ] GravityInvariantTest.FwdInteriorInvariantViolation_BoxEmpties
```

This is the regression test (`test/dreal/contractor/test/contractor_odes_semantic_test.cc`)
for the cav26 per-slice restoration (commit `5d619fe3f`) — it checks that an
invariant violated **only at a trajectory's interior** (not at either endpoint)
is still detected and refutes.

**The wrong first instinct** (caught in review) was to bump hull-grid back up
until the test passed (hull-8 passes). That is gaming the verifier: it hides the
defect behind the same fragile coupling instead of fixing it. See the amended
`rules/dont-game-the-verifier.md`.

## The scenario

Gravity flow `dx/dt = v, dv/dt = -1`, from a **point** initial condition
`x0 = 0, v0 = 1`, terminal pinned at `t = 2`. The exact trajectory is

```
v(t) = 1 - t           (exactly linear)
x(t) = t - t²/2        (exactly quadratic; peak x(1) = 0.5)
```

Invariant `∀t∈[0,2]. x ≤ 0.3`. It holds at both endpoints (`x(0)=x(2)=0`) and on
the terminal gate box, but is violated at the **interior peak** `x(1)=0.5 > 0.3`
(margin 0.2 — enormous vs the 1e-3 precision). Ground truth: **UNSAT**. A correct
per-slice filter refutes by finding a slice whose `x` enclosure lower bound
exceeds 0.3.

## The evidence (slice dump at hull-grid 4, order 12)

Instrumenting the per-slice invariant loop (`contractor_odes.cc`) to print each
slice's time span and state enclosure:

```
slice t=[0.0,0.5]  x=[0.000000, 0.500000]  v=[0.500000, 1.000000]  violated=0
slice t=[0.5,1.0]  x=[0.250000, 0.750000]  v=[0.000000, 0.500000]  violated=0
slice t=[1.0,1.5]  x=[0.250000, 0.750000]  v=[-0.500000, 0.000000] violated=0
slice t=[1.5,2.0]  x=[0.000000, 0.500000]  v=[-1.000000,-0.500000] violated=0
```

Read this carefully:

- There are **exactly 4 slices spanning [0,2]** → CAPD integrated the whole
  window in **one step of size 2.0** (the dynamics are trivial, so the adaptive
  step controller maximized the step), and hull-grid 4 split that one step into 4
  sub-intervals of width **0.5**.
- The **`v` enclosures are exact** (`v(t)=1-t` is linear; e.g. `[0.5,1.0]` over
  `t∈[0,0.5]` is the true range).
- The **`x` enclosures are ~4× too wide.** Over `t∈[0.5,1.0]` the TRUE range of
  `x` is `[x(0.5), x(1)] = [0.375, 0.500]` (x is monotone up to the peak), but
  the enclosure is **`[0.25, 0.75]`** — width 0.5 vs true 0.125. Its lower bound
  0.25 < 0.3, so the violation at the peak is **missed** on every slice.

`x(t)=t-t²/2` is an exact degree-2 polynomial and the IC is a point, so an
order-12 (let alone order-20) Taylor method has ~zero remainder — the *true*
enclosure should be essentially `[0.375, 0.500]`. The 4× blow-up is not a
fundamental interval-arithmetic limit; it is the **dependency problem** in
evaluating the Taylor curve over a wide time sub-interval.

At hull-grid 16 the same step is cut into width-0.125 sub-intervals; `curve(sub)`
over a narrow interval is tight enough that the near-peak slice has lb > 0.3, and
the test passes. **It passes by luck of the step size, not by design.**

## Root cause

```
sub_interval_width  =  (CAPD adaptive step size)  /  kHullGrid      ← fixed COUNT
enclosure looseness ≈  O(sub_interval_width²)   (polynomial dependency)
```

`kHullGrid` is a fixed **count per step**, so the sub-interval width — and hence
the per-slice enclosure tightness — is at the mercy of CAPD's adaptive step
controller. A trivial flow that admits a huge step gets wide sub-intervals and
loose enclosures. The per-slice invariant check refutes only when some slice's
`x`-enclosure lower bound clears the constraint, so its **refutation power is
coupled to `(step_size / hull_count)`** — a quantity no one is bounding.

## Why this is a real problem (not just this test)

1. **Interior-invariant detection silently weakens** when steps are large — the
   F1 test is one instance; production flows with large steps over a sharp
   interior excursion could be under-refuted at hull-16 too.
2. **The whole tube is looser than CAPD's precision**, so terminal-gate
   intersection and time-narrowing are weaker than they should be **corpus-wide**.
   The sweep's "speedups with zero flips" were partly measuring *less of an
   already-too-loose computation* — i.e. the baseline itself is leaving precision
   (and possibly solve-power) on the table.
3. It makes `--ode-hull-grid` a **soundness-relevant knob disguised as a
   performance knob.** Lowering it for speed quietly buys missed refutations.

### Soundness classification (important nuance)

This is **not** a false-`unsat` hazard. Lowering hull-grid only makes enclosures
*wider* (each sub-slice is still a sound outward over-approximation), so it can
never wrongly refute a feasible instance. The failure mode is the opposite:
**failure to refute an infeasible one** → returns `delta-sat` where `unsat` was
provable → a completeness / precision loss. For a *delta*-complete solver that is
not a soundness violation in the false-`unsat` sense, but the per-slice filter's
*entire purpose* is exactly this refutation, and a violation with margin 0.2 ≫ δ
should never be missed. The test author labels it a SOUNDNESS GATE because, for
the ODE contractor's contract, silently losing provable refutations is a
first-class defect.

## Proposed fix (not yet implemented)

Bound the sub-interval **width**, not the count:

```
n_sub = max(1, ceil(step_size / h_max))     // adaptive count
```

for a fixed maximum sub-interval width `h_max` (the `--ode-hull-*` flag becomes a
max-width rather than a count). Then `curve(sub)` is always evaluated over narrow
intervals regardless of CAPD's step size, so:

- per-slice enclosures stay tight (near CAPD's true precision) **independent of
  the step controller**;
- interior-invariant detection is robust by construction, not by luck;
- the tube tightens everywhere, likely *improving* narrowing/solve-power — which
  means the speed/precision trade should be **re-measured on the corrected tube**
  (the current sweep numbers are on the loose tube and must not be trusted for a
  default change).

### Alternatives considered

- **Refine-on-demand for the invariant check only** (subdivide a slice that
  straddles the invariant boundary until resolved). Fixes detection but leaves
  the terminal-narrowing tube loose; the width-based fix addresses both.
- **Cap CAPD's step (`--ode-max-step`)**. A band-aid: it fights the adaptive
  controller globally, hurt performance in the OFAT, and doesn't address curve
  looseness within whatever step remains.
- **A tighter CAPD range API** (if `curve` offers a non-naive range bound).
  Worth checking, but the width-based subdivision is robust to whatever `curve`
  does internally.

## Current state / decision

After the soundness-vs-completeness distinction was clarified (this is a
**completeness** tradeoff — missed refutation / false-`delta-sat` — never a
false-`unsat`), the owner **accepted the tradeoff** and the faster default was
**adopted**:

- **Default = forward order 12 / backward order 12 / hull-grid 4** (123-confirm:
  ~2× faster, PAR2 0.49, +4 solved, zero SAT↔UNSAT flips; bwd-12 adds ~5% over
  bwd-20). `config.h` carries the soundness note + a pointer to this file.
- The F1 regression test (`GravityInvariantTest.FwdInteriorInvariantViolation`)
  is **pinned to hull-grid 16** so it still guards the per-slice *mechanism*
  (interior violations ARE refuted at adequate resolution); it intentionally
  does **not** assert the hull-4 default catches this sub-resolution sharp case —
  the accepted completeness limit, documented here (the owner chose not to add a
  separate limitation test).
- All Phase-1 runtime-flag plumbing stays; the `--ode-*` flags let any workload
  override (e.g. order 16–20 + hull 16 for refutation-critical / sharp-invariant
  problems).

**The looseness defect itself is still open** (independent of the accepted
tradeoff): the tube is ~4× looser than CAPD's precision allows. The width-based
sub-slicing fix below remains the proper next step — it would recover the lost
refutation precision (and likely tighten narrowing corpus-wide) **while keeping
the speed**, after which the default would detect the F1 case too and hull-grid
would stop being completeness-relevant. Tracked as a follow-up.

## Reproduce

```bash
# temporarily set kDefaultOdeHullGrid{4} in src/dreal/solver/config.h, rebuild
cmake --build gcc_build --target dreal4_cmake_test -j8
gcc_build/dreal4_cmake_test --gtest_filter='GravityInvariantTest.*'   # fails at hull 4, passes at >= 8
# slice dump: re-add the DREAL_DEBUG_INV fprintf in the invariant loop of
# contractor_odes.cc (removed after this investigation) and re-run.
```

## Open questions for the fix

- What `h_max` keeps F1 tight with margin AND bounds the slice-count cost on
  large-step flows? (Probe both the gravity F1 margin and the k256 thermostat
  cost.)
- Does `capd::...Curve::operator()` have a tighter-than-naive interval range
  mode that would reduce the needed subdivision?
- After the fix, re-run the full OFAT + 123 — the corrected (tighter) tube may
  shift every knob's effect and could *itself* change solve counts.
