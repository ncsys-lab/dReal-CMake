# `CtcAcid` — adaptive 3BCID shaving (audit A, headline)

Header: [`ibex_CtcAcid.h`](../../../../ibex-fork/src/contractor/ibex_CtcAcid.h)
(`contractor/`). `class CtcAcid : public Ctc3BCid`. The **adaptive** version of
3BCID (Neveu, Trombettoni, Araya 2015) — it auto-tunes *how many* variables to
shave, so the user doesn't pick `vhandled`. **IBEX's own solver enables ACID by
default**, which is why it's the audit's top candidate.

> **dReal status:** **unused.** Strongest single lever to layer onto dReal's HC4
> contraction without touching the existing path. See [`../../AUDIT.md`](../../AUDIT.md) A.

## Constructors (verbatim from the header)

```cpp
CtcAcid(const System& sys, const BitSet& cid_vars, Ctc& ctc, bool optim=false,
        int s3b=default_s3b, int scid=default_scid,
        double var_min_width=default_var_min_width, double ct_ratio=default_ctratio);
CtcAcid(const System& sys, Ctc& ctc, bool optim=false,            // all variables
        int s3b=default_s3b, int scid=default_scid,
        double var_min_width=default_var_min_width, double ct_ratio=default_ctratio);
static constexpr double default_ctratio = 0.002;
```

- `sys` — a `System`, used to order variables by the *smearsumrel* criterion
  (which vars to shave first). **dReal must assemble a `System`** to use this
  (it currently builds per-constraint contractors; see integration note).
- `ctc` — the **sub-contractor** applied on each slice. For dReal this would be
  its HC4/fwd-bwd contractor (docs warn: *don't* use `Box` as sub-contractor;
  HC4 is the right choice).
- `ct_ratio` — the adaptive kernel: keep shaving variables until the average gain
  drops below `ct_ratio`. **Code default `0.002`.** ⚠️ The constructor's doc
  comment says "default value is 0.005" — the `static constexpr` (line 94) is the
  source of truth: **0.002**. (Recorded per source-fidelity; flag on upstream.)
- inherited `s3b`/`scid`/`var_min_width` — see [`Ctc3BCid.md`](Ctc3BCid.md).

## How it adapts (from the header's `contract` doc)

Alternates **tuning phases** (every `factor·nbinitcalls` nodes, for `nbinitcalls`
nodes: try `nbcidvar = min(max(2,2·nbcidvar), 5·nbvar)`, measure per-variable
gain, set `nbcidvar` to where average gain falls below `ct_ratio`) with **running
phases** (apply 3BCID to the first `nbcidvar` smear-ordered variables). So cost
self-regulates to the instance.

## Integration cost for dReal (the honest caveat)

1. Needs a `System` over the current Box's variables — dReal would have to build
   one (it already does for the dormant polytope path — reuse that assembly).
2. The sub-contractor must be dReal's HC4; ACID calls it many times per box, so
   it amplifies whatever the HC4 call costs (and the fork already drove that cost
   down — patches #1,#11). Net contraction is **stronger** (fewer search nodes),
   per-node cost **higher** — a classic completeness-vs-time trade; validate on
   the ODE/odeexpr families (`/benchmark`).
3. Runs inside DPLL(T) on transient literals — must respect dReal's
   empty-detection (`is_empty()`, post fork #11) and not break the backward
   callback contract (#2,#5,#6,#7). All present in the fork's IBEX; the work is
   dReal-side wiring in `generic_contractor_generator.cc`.

Related: [`Ctc3BCid.md`](Ctc3BCid.md) (the fixed-param parent),
[chapter](../../chapters/contractor.md), [`../../KNOBS.md`](../../KNOBS.md).
