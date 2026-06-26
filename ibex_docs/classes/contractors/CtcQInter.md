# `CtcQInter` — q-intersection (outlier-robust) contraction (audit D)

Header:
[`ibex_CtcQInter.h`](../../../../ibex-fork/src/contractor/ibex_CtcQInter.h)
(`contractor/`). `class CtcQInter : public Ctc`. Runs every contractor in a list
on a copy of the box, then returns the **q-intersection**: the smallest box
containing every point that lies in **at least `q`** of the contracted results.

> **dReal status:** **unused.** Robust-estimation tool, not a refutation tool — no
> dReal use shape. [`../../AUDIT.md`](../../AUDIT.md) D4.

## Constructor (verbatim)

```cpp
CtcQInter(const Array<Ctc>& list, int q);   // the list is NOT kept by reference
```

| Param | Meaning |
|---|---|
| `list` | the contractors (typically one per noisy measurement / constraint) |
| `q` | a point must satisfy **≥ q** of the contractors to survive |

## Algorithm (`ibex_CtcQInter.cpp` + `combinatorial/ibex_QInter.h`)

For each `i`, copy the box and run `list[i].contract(...)`, collecting the
contracted boxes; then `box = qinter(refs, q)`. The kernel
[`qinter(boxes, q)`](../../../../ibex-fork/src/combinatorial/ibex_QInter.h)
(`combinatorial/`, *"EXACT — Grid algorithm"*) computes that box exactly.

The point: tolerate up to **`n − q`** *faulty* constraints. If some measurements
are outliers, a plain intersection (all-`n`) would wrongly empty the box; the
q-intersection keeps whatever ≥ q agree on. This is parameter-estimation /
bounded-error localization (the SLAM-style use case in
[`../../chapters/example-slam.md`](../../chapters/example-slam.md)).

## Why dReal skips it

dReal **refutes** (proves `unsat` / finds a δ-model); it does not do robust
estimation against noisy redundant constraints. Every theory atom is a hard
constraint — there is no "outlier constraint to tolerate." Different problem
shape. Recorded so future sessions don't re-investigate.

Related: [contractor chapter](../../chapters/contractor.md),
[`../../chapters/example-slam.md`](../../chapters/example-slam.md).
