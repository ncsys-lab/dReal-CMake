# Set Representations — the wrapping-effect menu (comparison)

The set representation is the main lever on enclosure tightness (**completeness**) and
speed. A looser set can only *fail to refute* a `forall_t` invariant (a **COMPLETENESS**
loss); it can never produce a false `unsat` (**SOUNDNESS** is independent of the choice).
Background on *why* the choice matters: `../../concepts/dynsets-and-wrapping.md`,
`../../concepts/geomsets.md`. Authoritative catalog: `dynset/typedefs.h` (the
double/`NATIVE` `capdlib` build dReal uses).

> **`IVector` / `IntervalBox` is NOT in this menu.** The raw interval vector is the box you
> *seed* a set from and *read back* from it (`operator VectorType()`); propagating an
> `IVector` directly is the naive "product of intervals" form that "yields to big
> overestimations" (`geomset_module.html`) — i.e. maximal wrapping. Every type below is a
> wrapping-*controlled* propagation form. Don't conflate the seed box with a set.

---

> The QR-policy / reorganization column below is itself a composable knob with a
> much larger menu than the `Rect2`/`Pped2`/`Intv2` bundles named here — see
> [QRPolicies.md](QRPolicies.md) for the full 4-QR × 7-reorg catalog.

## (a) Reading a set name

CAPD names follow `[Class]Method[2][Details]Set` (`dynset_module.html`). Decode the three
independent axes:

| Axis | Token | Meaning |
|---|---|---|
| **Jet order** (which derivatives w.r.t. initial conditions are carried) | `C0` | position `φ` only — what dReal uses |
| | `C1` | position **+ Jacobian** `∂φ/∂x₀` (the monodromy/sensitivity matrix), via the C¹-Lohner algorithm (Zgliczyński, FoCM 2001) |
| | `C2` | + Hessian `∂²φ/∂x₀²` (C²-Lohner) |
| | `Cn` | + all derivatives up to order *n* (a jet) |
| **Geometry** (how the set is stored) | `Ball` | `x + Ball(r)` — a norm ball, no coordinate frame ("not recommended", `dynset_module.html`) |
| | `Affine` (`Rect`/`Pped`, no `2`) | `x + B·r` — single frame |
| | `2` = **doubleton** | `x + C·r0 + B·r` — `C·r0` = initial size, `B·r` = accumulated errors, in **two** frames |
| | (tripleton) `C0TripletonSet` | `x + C·r0 + intersection(B·r, Q·q)` — error term across **two intersected** frames |
| | `HO` | wraps a base set; intersects the **Taylor** step with the **Hermite–Obreshkov** step (a tighter *time*-remainder) |
| **Reorg / QR policy** (`Method` token) | `Rect` | orthogonalized frame (FullQR + pivoting). "best choice… especially when the eigenvalues are of a different magnitude. Overestimation… due to orthogonalization" (`dynset_module.html`) |
| | `Pped` | parallelepiped frame (InverseQR). "performs good in rotations and when all eigenvalues are of the same magnitude" but "in general… blow up quickly" (`dynset_module.html`) |
| | `Intv` | errors kept in a plain interval vector. "can be faster… but is prone to wrapping" (`dynset_module.html`) |
| | trailing `R` | built-in **R**eorganization (rebuild the frames before they go singular) |

So **`C1HORect2Set`** parses as: C1 jet (position + Jacobian) · HO time-enclosure ·
Rect2 doubleton/FullQR frame. **`C0Pped2Set`** = C0 · doubleton · parallelepiped frame.

---

## (b) Complete comparison table

One row per type in `typedefs.h`. **Tightness/cost are *relative* ordinal ranks** grounded
in the doc/header prose cited in the notes column, not measured numbers — treat as a
search order, not a guarantee. "Jet order" is the dominant cost axis (each order is roughly
another matrix/jet integrated per step); within a jet order, geometry and policy are the
finer levers.

| Type (`typedefs.h`) | Jet | Geometry | Reorg / QR policy | Tightness | Cost | Best / Worst for (grounded) | dReal status |
|---|---|---|---|---|---|---|---|
| `C0Set` | C0 | — (abstract) | — | — | — | abstract base; not instantiable | n/a |
| `C0BallSet` | C0 | `x+Ball(r)` | needs a `NormType` | lowest | low | norm-ball, `r=exp(Lip·h)+errors` (`C0BallSet.h`); no frame ⇒ heavy wrapping. "Ball… not recommended" (`dynset_module.html`) | not wired |
| `C0FlowballSet` | C0 | flow-ball | `NormType` | lowest | low | flow-adapted ball; "FlowBall… not recommended" (`dynset_module.html`) | not wired |
| `C0RectSet` | C0 | Affine `x+B·r` | FullQR (no reorg) | low | low | single-frame rect; can't split initial-size from error ⇒ wraps faster than a doubleton | not wired |
| `C0PpedSet` | C0 | Affine `x+B·r` | InverseQR | low | low | single-frame **Pped**: good near elliptic fixed point / equal-magnitude eigenvalues; "blow up quickly" in general (`dynset_module.html`) | not wired |
| `C0Intv2Set` | C0 | doubleton | `FactorReorganization<>` (plain, no QR on error frame) | low–med | **lowest of the doubletons** | **Intv** method: errors in a plain interval vector — "faster… but prone to wrapping" (`dynset_module.html`). Niche speed pick on benign flows | not wired (1 typedef away) |
| `C0Pped2Set` | C0 | doubleton | `FactorReorg<InverseQR>` | med | med | doubleton **Pped**: parallelepiped error frame — rotations / equal eigenvalues; degrades on different-magnitude eigenvalues | not wired (1 typedef away) |
| **`C0Rect2Set`** | C0 | doubleton | `FactorReorg<FullQRWithPivoting>` | med | med | doubleton **Rect**: "best choice… especially when the eigenvalues are of a different magnitude" (`dynset_module.html`). The general-purpose workhorse | **DEFAULT** (`--ode-c0-set rect2`) |
| **`C0TripletonSet`** | C0 | tripleton | `C0Rect2Policies` | med–high | med–high | error term `intersection(B·r, Q·q)` — never looser than the doubleton, usually tighter on strong rotation / disparate eigenvalues. **CAPD's own `DefaultC0Set`** (`typedefs.h`) | `--ode-c0-set tripleton` |
| **`C0HORect2Set`** = `C0HOSet<C0Rect2Set>` | C0 | HO over Rect2 doubleton | `C0Rect2Policies` | high | high (~2 integrations/step) | Taylor∩HO ⇒ tighter *time* remainder; best when time-discretization dominates tube width. Order ≤ 64, ≥ 3 (`C0HOSet.h`) | `--ode-c0-set horect2` |
| `C0HOTripletonSet` = `C0HOSet<C0TripletonSet>` | C0 | HO over tripleton | `C0Rect2Policies` | **highest C0** | high | stacks tripleton dual-frame **and** HO time-remainder = tightest available C0 set | exists, **NOT wired** (1 typedef away) |
| `C1Set` | C1 | — (abstract) | — | — | — | abstract base | n/a |
| `C1RectSet` | C1 | Affine | FullQR | — | C1-tier | single-frame C1; Jacobian carried but unsplit error frame | not wired (structural) |
| `C1PpedSet` | C1 | Affine | InverseQR | — | C1-tier | single-frame C1, Pped frame | not wired (structural) |
| `C1Rect2Set` | C1 | doubleton (both C0 & C1 parts) | `FactorReorg<FullQR>` | — | C1-tier | C¹-Lohner; position **and** Jacobian as doubletons. **`DefaultC1Set`** (`typedefs.h`) | not wired (structural) |
| `C1Pped2Set` | C1 | doubleton | `QRReorganization<InverseQR>` | — | C1-tier | C1 doubleton, Pped frame | not wired (structural) |
| `C11Rect2Set` | C1 | doubleton | `FactorReorg<FullQR>` | — | C1-tier | C¹-Lohner where the **derivative** part is moved by QR decomposition ("3rd method", `C11Rect2.h`) — an alternate C1Rect2 propagation, not a different geometry | not wired (structural) |
| `C1HORect2Set` = `C1HOSet<C1Rect2Set>` | C1 | HO over C1 doubleton | `C1Rect2Policies` | — | C1-tier + ~2× | C1 jet **and** HO time-remainder | not wired (structural) |
| `C1HOPped2Set` = `C1HOSet<C1Pped2Set>` | C1 | HO over C1 Pped doubleton | `C1Pped2Policies` | — | C1-tier + ~2× | C1 jet + HO, Pped frame | not wired (structural) |
| `C2Set` | C2 | — (abstract) | — | — | — | abstract base | n/a |
| `C2Rect2Set` | C2 | doubleton + Hessian | `FactorReorg<FullQR>` | — | C2-tier | C²-Lohner; position + Jacobian + Hessian | not wired (structural) |
| `C2Pped2Set` | C2 | doubleton + Hessian | `QRReorganization<InverseQR>` | — | C2-tier | C²-Lohner, Pped frame | not wired (structural) |
| `CnSet` | Cn | — (abstract) | — | — | — | abstract base | n/a |
| `CnRect2Set` | Cn | doubleton jet | `C2Rect2Policies` | — | Cn-tier (grows with order) | all derivatives to order *n* as doubletons (`CnRect2Set.h`); jet accessors | not wired (structural) |
| `CnMultiMatrixRect2Set` = `CnDoubletonSet` | Cn | doubleton jet (multi-matrix) | `C2Rect2Policies` | — | Cn-tier | Cn jet, derivatives w.r.t. a multiindex as doubletons (`CnDoubletonSet.h`) | not wired (structural) |

**Multiprecision boundary:** `mpcapdlib.h` defines `Mp*` analogues (`MpInterval`/`MpIVector`
+ Mp set typedefs) of every row above. They are **inapplicable** to dReal, whose build pins
`CAPD_INTERVAL_TYPE=NATIVE` (double-based intervals). Not enumerated here.

---

## (c) Recommendations — what to try, in order

The C0 axis is the cheap, no-new-plumbing lever (it is just an `--ode-c0-set` string or a
one-line typedef). The C1+ axis is a structural change to `contractor_odes_capd.cc`. Within
"grounded" below, **[doc]** = the CAPD docs state it; **[inference]** = my reasoned read of
the doc facts applied to dReal's setting (not a CAPD recommendation).

**Cheap C0 sweep (no structural change):**

1. **`C0Rect2Set` → `C0TripletonSet`.** The tripleton's `intersection(B·r, Q·q)` is provably
   ≥ as tight as the doubleton at modest extra cost, and it is **CAPD's own `DefaultC0Set`**
   — dReal defaults to the *cheaper* doubleton, a deliberate speed-over-tightness call worth
   re-examining per family. **[doc]** for "DefaultC0Set = tripleton"; **[inference]** that
   dReal should adopt it. (`AUDIT.md` B1.)
2. **Wire `C0HOTripletonSet`** (currently absent from the flag, 1 typedef away). Stacks the
   tripleton frame and the HO time-remainder ⇒ tightest available C0 set; ~2× integration
   cost, best when time-discretization dominates the tube. **[inference]** from the HO and
   tripleton mechanisms. (`AUDIT.md` B2.)
3. **Niche policy pickers** if a family misbehaves: `C0Pped2Set` near elliptic fixed points /
   equal-magnitude eigenvalues **[doc]**; `C0Intv2Set` for raw speed on benign,
   well-conditioned flows (accepts more wrapping) **[doc]**. Both are 1 typedef away. Also
   `SelectiveQRWithPivoting` (orthogonalize only near-parallel vectors → same enclosure when
   nothing is near-parallel, cheaper otherwise) is a pure speed lever but requires exposing
   the QR policy (`classes/sets/Rect2QRPolicy.md`).

**Structural C1 step (high value / high effort):**

4. **A C1 set + C1 solver** carries the flow **Jacobian** `∂φ/∂x₀` — exactly the sensitivity
   an interval-Newton / mean-value **backward-narrowing** step needs to sharpen the
   initial-condition box `X₀` from a terminal constraint (dReal currently narrows the C0 tube
   with ibex HC4 only). `C1Rect2Set` is CAPD's `DefaultC1Set`. Cost ≈ O(dim²) matrix
   integration per step + a log-norm bound at construction + new plumbing. **[inference]**;
   the per-step mechanism is **[doc]**. (`AUDIT.md` A; `classes/sets/C1DoubletonSet.md`.)

C2/Cn sets exist for higher-order variational data (Hessian, full jets) but have no
identified use in dReal's value-then-narrow loop — listed for completeness only.

---

## (d) Fidelity notes — what is NOT confirmed

Confirmed in doc text or header for **every** capability claim above are: the geometry forms,
the policy bindings (read directly from `typedefs.h`), the Rect/Pped/Intv/Ball method
tradeoffs (`dynset_module.html` verbatim), the tripleton intersection and HO Taylor∩HO
mechanism + order caps (headers), and the C¹/C²-Lohner attributions (headers).

Marked **unverified / inference, not sourced**:

- **All "Tightness" and "Cost" ordinal ranks** are reasoned from the mechanism descriptions,
  **not measured** and **not stated as an ordering by CAPD**. The only ordering CAPD asserts
  is the *method* qualitative one (Rect best general / Pped niche / Intv faster-but-wraps /
  Ball not recommended) and that the tripleton is its default C0 set. C1+ rows are left blank
  rather than guessed — there is no doc basis to rank a C1 set's *tightness* against a C0 one
  (different information, not strictly comparable).
- **`C11Rect2Set` vs `C1Rect2Set`**: both bind `FactorReorganization<FullQRWithPivoting>`.
  The only sourced distinction is the header phrase "derivative of the flow moved via QR
  decomposition (3rd method)" (`C11Rect2.h`) — i.e. a different *propagation* of the C1 part,
  same geometry. Whether this is tighter, faster, or merely an alternate is **unverified**.
- **`C0Intv2Set` cost = "lowest of the doubletons"**: grounded only in the qualitative "Intv…
  can be faster than other methods" (`dynset_module.html`); the *relative* placement among
  doubletons is **inference**.
- **HO "~2 integrations per step"** is an inference from "intersection of the Taylor method
  and the Hermite–Obreshkov method" (both must be computed); CAPD does not state a 2× cost
  figure.

## (e) Header enumeration (ground truth — re-auditable)

The table above is complete iff it has a row for every typedef below. Re-audit with
`grep -nE 'typedef.*Set;|using DefaultC[0-9]Set' gcc_build/capd-install/include/capd/dynset/typedefs.h`:

```cpp
// C0 (value-only)
C0Set  C0BallSet  C0FlowballSet
C0PpedSet  = C0AffineSet<…,C0PpedPolicies>      // single frame, InverseQR
C0RectSet  = C0AffineSet<…,C0RectPolicies>      // single frame, FullQR
C0Intv2Set = C0DoubletonSet<…,C0Intv2Policies>  // doubleton, plain reorg
C0Pped2Set = C0DoubletonSet<…,C0Pped2Policies>  // doubleton, InverseQR
C0Rect2Set = C0DoubletonSet<…,C0Rect2Policies>  // doubleton, FullQR     ← dReal default
C0TripletonSet = C0TripletonSet<…,C0Rect2Policies>                       // ← DefaultC0Set
C0HORect2Set     = C0HOSet<C0Rect2Set>
C0HOTripletonSet = C0HOSet<C0TripletonSet>      // exists, NOT wired
DefaultC0Set = C0TripletonSet<…,C0Rect2Policies>   // CAPD's own default
// C1 (+Jacobian ∂φ/∂x₀)
C1Set  C1RectSet  C1PpedSet
C1Rect2Set = C1DoubletonSet<…,C1Rect2Policies>   // DefaultC1Set
C1Pped2Set = C1DoubletonSet<…,C1Pped2Policies>
C11Rect2Set                                       // 3rd-method derivative propagation
C1HORect2Set = C1HOSet<C1Rect2Set>   C1HOPped2Set = C1HOSet<C1Pped2Set>
DefaultC1Set = C1DoubletonSet<…,C1Rect2Policies>
// C2 (+Hessian) / Cn
C2Set  C2Rect2Set  C2Pped2Set
CnSet  CnRect2Set  CnMultiMatrixRect2Set (= CnDoubletonSet)
```
(Multiprecision `Mp*` analogues live in `mpcapdlib.h` — inapplicable to the NATIVE build.
`Fad*` = nonrigorous, excluded. See `../../KNOBS.md` audit.)

## Source

[dynset_module.html](../../../../CAPD/docs/html/dynset_module.html) ·
[geomset_module.html](../../../../CAPD/docs/html/geomset_module.html) ·
[dynset_reorganizedset.html](../../../../CAPD/docs/html/dynset_reorganizedset.html) ·
ground truth: `gcc_build/capd-install/include/capd/dynset/typedefs.h`
