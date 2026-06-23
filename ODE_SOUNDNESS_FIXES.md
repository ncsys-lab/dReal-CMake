# ODE Soundness Investigation — running log

> Cross-session findings log for the CAPD ODE-contraction soundness work on branch
> `rounding-mode-fixes` (HEAD `5d619fe3f`). Append findings as they're made. The plan lives in
> `~/.claude/plans/what-is-going-on-calm-dewdrop.md`. **Goal: understand the CAPD per-slice ODE
> contractor deeply enough to rewrite it with ownership and fix the false-unsats — NOT a blind
> port of cav26 (cav26 is undocumented decade-old dReal3 code AND has its own false-unsat, see
> below).**

## Binaries / oracles (provenance matters)
- **HEAD** = `gcc_build/dreal4`, clean rebuild of `5d619fe3f` (the committed per-slice contractor).
- **cav26** = `/usr/local/bin/dreal4_cav26` (Jun 7 build). The previously-"trusted" reference.
- **main** = `/tmp/dreal4_main` (coarse-endpoint ancestor). **Its printed ODE model is internally
  inconsistent** (see Finding 5) — do NOT trust main/cav26 printed continuous ODE values.
- All solver runs: under `oom_killer.sh` + a `timeout`. CPU-time metric.

## ★ CRITICAL: there are TWO DIFFERENT bugs — neither implementation is a clean oracle ★

| Reproducer | TRUE verdict | HEAD | cav26 | Who's wrong |
|---|---|---|---|---|
| `github …k32_water_water-triple-network.drh.n` | delta-sat | **UNSAT** ✗ | delta-sat ✓ | **HEAD** |
| `github …k64_water_water-triple-network.drh.n` | delta-sat | **UNSAT** ✗ | delta-sat ✓ | **HEAD** |
| `github …k64_thermostat_…-triple-network.drh.n` | delta-sat | **UNSAT** ✗ | delta-sat ✓ | **HEAD** |
| `ode_soundness_repros/bug003_bad.smt2` | delta-sat | delta-sat ✓ | **UNSAT** ✗ | **cav26** |
| `ode_soundness_repros/wstep_drain.smt2` | delta-sat | delta-sat ✓ | **UNSAT** ✗ | **cav26** |

**cav26 false-unsats** the ZOH-parameter-coupling pattern (documented as BUG-003 in
`simulink-to-dreal/docs/dreal-bugs.md`; "a cav26 regression vs dReal v3.16.12"). HEAD's
from-scratch `par:`/`setParameter` var/par split **fixed** that — so the split is load-bearing,
must be kept. Conversely HEAD introduced a false-unsat on the multi-step water/thermostat
automata. **You cannot fix this by restoring cav26.** Source: A/B
`benchmark/results/ab_20260622_162840/compare.txt` (main vs HEAD) for HEAD's 3 false-unsats.

### Reproduce the cav26 false-unsat (the critical, easily-lost fact)
```bash
cd dreal4-cmake
# bug003: d/dt[x]=y, d/dt[y]=0, x_0=20,y_0=30,t=1 => x_t=50; assert x_t>=49 => TRUE=delta-sat
/usr/local/bin/dreal4_cav26 ode_soundness_repros/bug003_bad.smt2   # => unsat  (WRONG)
./gcc_build/dreal4          ode_soundness_repros/bug003_bad.smt2   # => delta-sat (correct)
# wstep_drain: faithful 1-step water flow_0, drain mode, gate x_t<=5 (drain from 5 => TRUE=delta-sat)
/usr/local/bin/dreal4_cav26 ode_soundness_repros/wstep_drain.smt2 # => unsat  (WRONG)
./gcc_build/dreal4          ode_soundness_repros/wstep_drain.smt2 # => delta-sat (correct)
```

## ★ Cleanest reproducer pair for HEAD's bug: clock-rate diff ★
The benchmark dir has a `-sat` sibling HEAD solves correctly and the non-`-sat` one HEAD
false-unsats — **both genuinely SAT**, and they differ by a SINGLE thing: the clock RHS.
`~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/`
- `…-triple-network-sat.drh.n.smt2` (HEAD ✓): `d/dt[tau] = (+ (* 1 γ…) …)` → rate 3 (fill) →
  reaches `tau=1` at `t=1/3` (terminal in the *interior* of the [0,1] integration window).
- `…-triple-network.drh.n.smt2` (HEAD ✗ false-unsat): `d/dt[tau] = (+ (* (/ 1 3) γ…) …)` → rate 1
  → reaches `tau=1` at `t=1` (terminal at the *end* `win_ub` of the integration window).
Identical tank dynamics, identical everything else (`diff` = only the `(/ … 3)` clock tokens).
**HEAD false-unsats only when the terminal time sits at the end of the integration window** /
the integration is ~3× longer. ← current prime hypothesis.

## The water benchmark, decoded
3-tank hybrid automaton, `QF_NRA_ODE`, 33 chained `(integral 0. time_k [X_t] [X_0] flow_k)` steps.
- State vec (16): `[A1 A2 A3 q1 q2 q3 tau x1 x2 x3 γ×6]`. `d/dt=0` for the 12 `A,q,γ` → CAPD
  **parameters**; nonzero `d/dt` for `tau,x1,x2,x3` → true ODE state.
- Tank ODE (fill mode `γ_X_2=1`): `d/dt[x] = (q − 0.5·√(2g)·√x)/A`, `g=9.80665`; drain mode
  (`γ_X_1=1`): `−0.5·√(2g)·√x/A`. Coupled: `x2` RHS uses `√x1`, `x3` uses `√x2`. Has **sqrt
  nonlinearity** (∞ slope near 0), **param-in-divisor**, **γ-multiplier params**.
- `tau` is a clock; `forall_t` invariant = `tau ∈ [0,1]` along the trajectory.
- `x1,x2,x3 ∈ [0,10]` (declared domain). **`x=5` is a mode-transition GUARD threshold**
  (`(>= x_0_t 5)` vs `(< x_0_t 5)` select the next step's mode) — NOT a hard gate. The `[0,5]`
  seen in earlier debug was a guard-branch sub-box.

## Findings (chronological)

**F1 — HEAD's 3 false-unsats are real** (A/B compare.txt). Catastrophic direction for a
delta-complete solver. HEAD is also 2.1× slower than cav26 on the ODE family.

**F2 — Logically-forced lever.** Invariant-off, water still refutes ⟹ refutation = "no terminal
slice's enclosure ∩ gate survives." Sound tube + sound hull-narrowing can NEVER refute a SAT
instance ⟹ HEAD's tube fails to contain a valid trajectory at some step, OR the filter/narrowing
is unsound. The defect is concrete and findable.

**F3 — BUG-005 (decay endpoint) is NOT the bug — opposite failure mode.** `d/dt[x]=−0.5x`,
`x_0=100`, `t=1` ⟹ true `60.653`. HEAD `--model` reports `x_0_t∈[61.597,61.598]` (the doc's
signature), but the discriminating test `(<= x_0_t 61)` (true `60.653≤61`) returns **delta-sat**
with `x_0_t∈[60.486,60.487]` — so HEAD's contractor admits a WIDE band `~[60.49,61.60]` that
CONTAINS the truth; it's **over-wide (sound, imprecise), not over-pruning**. `--vis`
(`run_capd_trace`) reports the tight correct `60.653`. Root of the width: `integrate_tube_slices`
uses `curve(sub)` over each time *sub-interval* (a range), while `run_capd_trace` reports the
value *at* each `t_i` (a point). cav26 uses `curve(subsetOfDomain)` identically → shared, sound.
Repros: `ode_soundness_repros/bug005.smt2`, `bug005_disc.smt2`. **Ruled out.**

**F4 — cav26 false-unsats ZOH coupling (see the ★ table). Critical.** Repros: `bug003_bad.smt2`,
`wstep_drain.smt2`.

**F5 — `main`'s printed ODE model is internally INCONSISTENT** (can't be used as a witness):
reports `tau_0_t=1` with `time_0=0.5` and a rate-1 clock (should be `0.5`); `x1_0_t=2.5`
contradicts the near-equilibrium dynamics. The instance is SAT (verdict), but the printed
trajectory is not valid. Get no witness from main/cav26 printed continuous values.

**F6 — HEAD is correct on every SINGLE-step synthetic case I built** (single tank at equilibrium,
faithful 1-step drain). HEAD only false-unsats the real MULTI-STEP automata ⟹ the bug is in the
**chaining** (step k's narrowed `X_t` → step k+1's `X_0`, via time-narrowing or the **BWD**
contractor) and/or the **terminal-at-window-end** clock case (★ pair) — not a single integral's
tube. Repro that does NOT break HEAD: `ode_soundness_repros/wtank1.smt2`.

**F7 — The HEAD-breaking knob is the clock rate / integration length (★ pair).** `-sat`
(rate 3, terminal at t=1/3, interior) HEAD ✓; non-`-sat` (rate 1, terminal at t=1=`win_ub`)
HEAD ✗. Confirmed and refined by F8.

**F8 — ★ MINIMAL SYNTHETIC REPRODUCER + the mechanism. ★** A faithful 1-step water flow_0
(coupled 3-tank, drain mode, free `time`) false-unsats on HEAD iff **all three** hold:
(a) coupled nonlinear dynamics (NOT a lone clock), (b) a **point/degenerate terminal gate**
`tau_t = [1,1]`, and (c) the terminal lands at the **end of the integration window** (`win_ub`).
Repro `ode_soundness_repros/ws_taupin.smt2` (window `[0,1]`, gate `tau_t=[1,1]`): HEAD **unsat**,
genuinely delta-sat (drain from 5 ⇒ `x<5`, `tau=1` at `t=1`).

Discriminating ladder (all genuinely SAT; HEAD verdict):
| variant | tanks | window | tau gate | terminal | HEAD |
|---|---|---|---|---|---|
| `clock_only.smt2` | no (clock only) | [0,1] | `[1,1]` | win_ub | delta-sat ✓ |
| `ws_taupin_wide.smt2` | yes | [0,1.1] | `[1,1]` | interior | delta-sat ✓ |
| `ws_boundary.smt2` | yes | [0,1] | `[0.999,1.0]` | win_ub | delta-sat ✓ |
| **`ws_taupin.smt2`** | yes | [0,1] | `[1,1]` | **win_ub** | **unsat** ✗ |

Gate-bracketing on `ws_taupin` (tanks, window [0,1], terminal = win_ub):
`[0.99,1.0]`→delta-sat; `[0.9999999999,1.0]`→unsat; `[1.0,1.0]`→unsat; `[1.0,1.01]`→unsat.
⟹ the contractor's **terminal-slice `tau`-enclosure upper bound lands strictly SHORT of
`win_ub=1.0`** by a non-tiny margin (in `[0.99, 0.9999999999)`), NOT mere rounding. A gate that
reaches below ~0.99 catches a surviving slice; a gate pinned at/near `1.0` finds none → false
refute. Widening the window so the terminal becomes interior (a later slice straddles the point)
fixes it — explaining why the rate-3 `-sat` sibling (reaches `tau=1` at `t=1/3`, interior)
survives while the rate-1 non-`-sat` (`tau=1` at `t=1=win_ub`) refutes. **The per-slice tube does
not include the terminal time point when the terminal coincides with the integration end.**

Mechanism HYPOTHESIS (not yet code-confirmed — deferred per the Phase-3 pause): the last
per-slice enclosure, built from `curve(sub)` over the **clamped final CAPD step** (the step CAPD
shortens to land exactly on `t_ub`), under-covers the step-end time — so the tube's last slice
falls short of `win_ub`. Candidates to check in `integrate_tube_slices` (contractor_odes_capd.cc):
`solver.getStep()` returning the natural (un-clamped) step after the clamped final step;
`curve(sub)` evaluated over the wrong sub-domain on the last step; or the `do/while
(!completed())` loop dropping/short-cutting the final slice. Confirm by instrumenting the last
slice's `(t_lb,t_ub, tau-enclosure)` on `ws_taupin` (a Phase-2 follow-up; needs a debug rebuild).

**F9 — ★★ ROOT CAUSE FOUND + FIXED: `to_capd_string` 6-digit truncation. ★★**
The defect is in the integration **feed**, not the per-slice filter. `to_capd_string(double)`
(`src/dreal/contractor/odes/to_capd_string.h`) rendered constants with `std::to_string`, which
emits only **6 fractional digits** (sprintf `%f`). So the clock coefficient `(/ 1 3) =
0.3333333333333333` became the literal string `"0.333333"`, and CAPD integrated `d/dt[tau] =
3·0.333333 = 0.999999` — a vector field **unfaithful by 1e-6**. tau(1) tops at `0.999999 < 1.0`,
so a terminal gate at/near `tau=1` (a clock reaching its bound at the integration-window end)
finds no surviving slice → false-`unsat`. Confirmed by instrumenting `integrate_tube_slices`:
the last slice on `ws_taupin` was `t=[0.996,1.0]` but `state0(tau)=[…, 0.99999900000000009]`,
exactly the 1e-6 deficit; params dumped as exact `[1,1]`/`[0,0]`, so the deficit is the string,
not the box.

**Why it round-trips once fixed:** CAPD parses a decimal literal into an *outward-rounded*
interval. The full-precision `"0.33333333333333331"` (17 sig figs = `max_digits10`) parses to an
interval that **brackets** the true 1/3, so `3·` brackets 1.0 and the `[1,1]` gate is hit. The
truncated `"0.333333"` parses strictly below 1/3.

**Fix** (test-first; `test/dreal/contractor/test/to_capd_string_test.cc::ConstantRoundTripsExactly`
fails→passes): format at `max_digits10` (17) sig figs in default notation; scientific (|v|<1e-4 or
huge) re-rendered as `fixed`-`setprecision(40)` since CAPD's parser is unreliable on `1e-3` forms;
added `DREAL_ASSERT_ROUNDING(FE_TONEAREST)` — decimal formatting is correctly-rounded only in
nearest, and the sole caller (`build_imap_strings` ← `make_capd_ode_cache`) already runs under a
`NearestRoundingScope`, so the assert verifies the inherited mode (cf. `format_double`). 14 golden-
string tests ported from the old 6-digit format (`"3.500000"`→`"3.5"`, `"x^1.000000"`→`"x^1"`, …).

**Impact:** all **3** committed-HEAD false-`unsat`s flip to `delta-sat` (`k32_water…`,
`k64_water…`, `k64_thermostat…`-triple-network), matching cav26/ground truth. `ws_taupin` and the
synthetic repros all correct. ~15-line feed fix — **NOT** the contractor rewrite the investigation
set out to do.

**The bug is longstanding and SHARED, not a rewrite regression.** cav26's `to_capd_string` (via
`git show cav26:…/to_capd_string.h`) uses the *same* `std::to_string`; `main` (coarse-endpoint)
too. Hypothesis (consistent, not yet fully proven): `main`'s coarse-hull and cav26's looser
per-slice filter (no `slice.t_lb <= win_ub` terminal-eligibility upper bound — see
`contractor_odes.cc:396` vs cav26 `filter`) were loose enough to **mask** the 1e-6 deficit; HEAD's
tighter per-slice tube **exposed** it. So the per-slice filter is likely *sound* and the "rewrite
is buggy" framing was a misattribution — pending the full cav26-oracle A/B (Phase 4) to confirm
0 SAT↔UNSAT disagreements remain. ws_taupin/ws_boundary were over-minimized: BOTH HEAD and cav26
false-`unsat` them pre-fix (degenerate point gate at the truncation boundary), so they did NOT
isolate the HEAD-vs-cav26 distinction — F8's "minimal reproducer" caveat was right to flag this.

**F10 — Two unit tests encoded the same false-unsat; rounding-mode test isolation.**
`ContractorCapdFullTest.{CapdFwd,CapdBwd}` (`contractor_capd_test.cc`) asserted the box
**empties** on `x'=1, p'=(1/√2π)e^{-x²/2}` with `xt=10, pt∈[0,1]`. But `p(20)=Φ(10)−Φ(-10)=
erf(10/√2)` is **strictly < 1**, so `(x=10, p≈0.99999998∈[0,1])` is a genuine witness → the
sound verdict is **delta-sat**, and the old "empties" was the to_capd_string-truncation
false-unsat (the unfaithful `1/√2π→0.398942` field). With the fix, both narrow to the real
witness — instrumented: FWD `t0∈[19.93,20.02], pt=[0.99999999711,1]`; BWD `x0=−10, p0=[0,2.9e-9]
=1−∫gaussian, t0∈[19.93,20.02]`. Expectations rewritten to `ASSERT_FALSE(empty)` + witness-band
narrowing checks (`t0∋20`, gate respected), comments corrected (the per-slice filter is sound; the
feed lied). All 35 CAPD/ODE/semantic tests green.

Also (★ the rounding-mode angle): `to_capd_string` now `DREAL_ASSERT_ROUNDING(FE_TONEAREST)` and
its 17-sig-fig decimal formatting is **mode-sensitive** (binary→decimal is correctly rounded only
in nearest). `ToCapdStringTest` calls it directly with no scope, so it inherited a *prior* CAPD
test's leftover `FE_UPWARD` → the round-trip check failed (and would abort the Debug gate).
Production always reaches `to_capd_string` under `make_capd_ode_cache`'s `NearestRoundingScope`;
the tests now mirror that via a fixture member `NearestRoundingScope nearest_` (TEST→TEST_F). (The
leftover-`FE_UPWARD` from some earlier ODE test is a benign pre-existing test-hygiene quirk —
invisible until a *mode-sensitive, scope-less* direct call surfaced it; production code always
establishes its own regime.)

## Verification (Phase 4 — the fix is confirmed)

- **cav26-oracle A/B**, 123 ODE-family jobs (`benchmark/results/ab_20260622_190607/compare.txt`,
  `do_ab.sh /usr/local/bin/dreal4_cav26 gcc_build/dreal4`): fixed-HEAD has **0 false-`unsat`s**.
  The 3 committed-HEAD false-`unsat`s are gone — `k32_water`/`k64_water` now agree `delta-sat` with
  cav26; `k64_thermostat` is a *disagreement where fixed-HEAD is right*. The **only 2 SAT↔UNSAT
  disagreements** are both `cav26=UNSAT` / `fixed=delta-sat` on the thermostat-triple-network family
  (`k256…-sat` is generator-labeled SAT → cav26 false-`unsat`s it; `k64…`), i.e. fixed-HEAD **fixes
  2 of cav26's own residual false-`unsat`s** (cav26 carries the same `to_capd_string` truncation).
  Net: fixed-HEAD solves **118/123 vs cav26's 109**, PAR2 **0.39×** (≈2.5× faster aggregate).
- **Debug rounding gate** (`./rounding_debug_gate.sh` + full `cmake-build-debug` ctest): lint clean,
  **no rounding-assertion abort** (the new `to_capd_string` `FE_TONEAREST` assert holds suite-wide),
  **630/630 pass** including `CapdFwd`/`CapdBwd`/`ConstantRoundTripsExactly`.
- **Open perf follow-up (NOT soundness):** 3 benchmarks fixed-HEAD TIM'd that cav26 solved
  (`k128_quad`, `k2_battery`, `k2_prostate_h2`) — timeouts, not wrong verdicts. Unknown whether
  introduced by the faithful (longer) field strings, pre-existing in committed HEAD, or A/B
  contention; the net is +9 solves and 2.5× faster, so deferred.

## Ruled out
- Var/IMap ordering mismatch (`vars_0_` is `ode_list`-order filtered to non-pars; aligned).
- Tolerance (`1e-10` vs cav26 `1e-20`) as the false-unsat cause: looser tol only WIDENS a sound
  tube → less refutation, wrong direction. (Still a real precision divergence to restore.)
- BUG-005 / decay endpoint (F3).
- Single-integral tube/feed (F6).

## Code map (for the rewrite)
- `src/dreal/contractor/odes/contractor_odes.cc` — `contractor_ode_lohner::Prune` (the per-slice
  filter + narrowing; the set_empty refutation at `if (!have_keep)`), `generate_trace`.
- `src/dreal/contractor/odes/contractor_odes_capd.cc` — `integrate_tube_slices` (contractor tube),
  `run_capd_trace` (the `--vis` tube, known-correct), `build_imap_strings`/`with_params` (the
  par/var feed), `make_capd_ode_cache`.
- cav26 reference (read-only): `/tmp/cav26_ode/{contractor_odes.cc,capd_helpers.h}`.
- Parse/structure: `src/third_party/com_github_robotlocomotion_drake/dreal/symbolic/odes/`
  (`FormulaIntegral` ctor at `symbolic_odes.cc:184`).
