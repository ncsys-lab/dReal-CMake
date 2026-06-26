# KNOBS.md — the complete IBEX tuning surface, audited

Every IBEX-side tunable that affects dReal's contraction, its option menu, the
IBEX default, and **dReal's current status**. Audit-by-construction: if a knob
isn't here, it isn't being left on the table. Defaults are confirmed against the
fork headers (cited per row); the `.rst` docs leave several "to be completed", so
**the header is ground truth**.

Legend — dReal status: 🟢 active · 🟡 present-but-off/dormant · ⚪ unused (no code) ·
🔒 fixed at default.

## 1. Build-time flags (the two CMake knobs)

| Knob | Menu | dReal value | Status | Note |
|---|---|---|---|---|
| `INTERVAL_LIB` | gaol / filib / … | **gaol** | 🟢🔒 | the patched backend (fork #8–#12); arm64-native. `CMakeLists.txt:201` |
| `LP_LIB` | none / soplex / clp | **none** | 🟡 | *"dReal does not exercise IBEX's LP-based contractors"* — this is **why the polytope path is dormant**. `CMakeLists.txt:202` |

## 2. Active path — HC4 forward-backward + propagation

dReal runs the HC4 algorithm but through its **own** fixpoint loop, so the IBEX
class defaults below are the *reference*, not literally dReal's values.

| Knob | Class · header | Menu / range | IBEX default | dReal status |
|---|---|---|---|---|
| fixpoint ratio (propagation) | `CtcPropag` | (0,1) | **0.01** (constexpr; a stale comment says 0.1) | 🟢 own worklist; analog knob in `contractor_worklist_fixpoint.cc` |
| fixpoint ratio (plain) | `CtcFixPoint` | (0,1) | **0.1** | 🟢 analog in `contractor_fixpoint.cc` |
| `accumulate` flag | `CtcPropag` | bool | false | ⚪ — "slightly tighter, a little slower" |
| input/output bitsets | `Ctc` | per-var | unset | 🟢 own analog (skip-when-can't-fire) |
| backward callback | `Function::backward` | on/off | (fork-added) | 🟢 **on** — drives theory lemmas (fork #2/#5/#6/#7) |

## 3. Unused atomic contractors — the opportunity surface (audit A/D)

| Knob | Class · header | Menu / range | IBEX default | dReal status |
|---|---|---|---|---|
| **`s3b`** (max shaved slices) | `Ctc3BCid` | int, best **5–200** | **10** — *"tune in priority"* | ⚪ **audit A** |
| `scid` (CID slices) | `Ctc3BCid` | int | **1** — *"don't touch"* | ⚪ |
| `vhandled` (vars/contract) | `Ctc3BCid` | int / −1=all | −1 | ⚪ (ACID computes this) |
| `var_min_width` | `Ctc3BCid` | double | **1e-11** | ⚪ |
| **`ct_ratio`** (adaptive kernel) | `CtcAcid` | double | **0.002** (constexpr; ctor *comment* says 0.005 — code wins) | ⚪ **audit A headline** |
| `optim` (objective-first) | `CtcAcid` | bool | false | ⚪ (n/a for SMT) |
| **`ceil`** (Newton gate) | `CtcNewton` | double | **0.01** | ⚪ audit D |
| `prec`, `gauss_seidel_ratio` | `CtcNewton` | double | lib defaults | ⚪ audit D |

## 4. Dormant path — polytope hull + X-Taylor (audit D; needs `LP_LIB`≠none)

| Knob | Class · header | Menu / range | IBEX default | dReal status |
|---|---|---|---|---|
| `--polytope` / `--forall-polytope` | dReal `config` | bool | — | 🟡 **off** (`config.h:240-241`) |
| `max_iter` | `CtcPolytopeHull` | int | 100 | 🟡🔒 |
| `time_out` | `CtcPolytopeHull` | seconds | 100 | 🟡🔒 |
| `eps` | `CtcPolytopeHull` / `LPSolver` | double | **1e-9** (`LPSolver::default_tolerance`; the `CtcPolytopeHull.h` doc-comment's "1e-10" is stale) | 🟡🔒 |
| `mode` | `LinearizerXTaylor` | RELAX / RESTRICT | RELAX | 🟡🔒 |
| `corners` | `LinearizerXTaylor` | INF / SUP / RANDOM / RANDOM_OPP | RANDOM_OPP | 🟡🔒 unexplored |
| `slope` | `LinearizerXTaylor` | TAYLOR / HANSEN | HANSEN | 🟡🔒 unexplored |
| linearizer choice | `Linearizer*` | XTaylor / Compo / Duality / Fixed | XTaylor | 🟡 (`Affine2` **not built** — plugin) |

## 5. Ignored by design — search strategy (dReal branches inside DPLL(T))

| Knob | Class | Menu | dReal status |
|---|---|---|---|
| bisector | `Bsc` family | LargestFirst / RoundRobin / SmearMax/Sum/SumRelative / LSmear | ⚪ dReal branches itself |
| bisection `ratio` | `Bsc` | `Bsc::default_ratio()` | ⚪ |
| per-var precision | `Bsc` | scalar / `Vector` | ⚪ |
| cell buffer | `CellStack` / `CellHeap` / … | DFS / best-first | ⚪ own search stack |

## Coverage audit

- **The micro-optimization budget of the active path is spent** — fork patches
  #1,#9,#10,#11 already pulled the rounding/exception/gradient levers (numbers in
  [`dreal-ibex-usage.md`](dreal-ibex-usage.md)). No §2 knob is a likely big win.
- **The real unexplored surface is §3** (ACID/3BCID `s3b`/`ct_ratio`, Newton
  `ceil`) — contractors dReal doesn't run at all.
- **§4 is gated on a build decision** (`LP_LIB=none`); its knobs are moot until
  that flips.
- The prioritized read of all this is [`AUDIT.md`](AUDIT.md).
