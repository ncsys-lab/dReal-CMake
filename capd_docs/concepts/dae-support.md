# DAEs (differential-algebraic equations) and dReal — orientation

> **Status of this node:** part **verified fact** (what CAPD does/doesn't offer),
> part **design reasoning** (how dReal *could* support DAEs). The two are kept
> explicitly separate below — the routes are my own analysis, not sourced from any
> CAPD doc.

## Verified fact: CAPD has no DAE support

A search of the full CAPD Doxygen docs **and** the installed headers
(`gcc_build/capd-install/include/capd/`) for `DAE` / "differential-algebraic" /
"algebraic constraint" / "index-1" / "semi-explicit" returns **nothing**. CAPD is
an ODE / dynamical-systems library: it rigorously integrates `x' = f(x)` (and the
dissipative-PDE extension — see [dissipative-pdes.md](dissipative-pdes.md)), but
it has no notion of coupled algebraic constraints, no index reduction, and no
consistent-initialization machinery. So there is **no CAPD reference tree to build
for DAEs** — building one would mean fabricating an API that does not exist.

## What a DAE is (for framing the routes)

A semi-explicit DAE couples a differential part with an algebraic constraint:

```
x' = f(x, y)        (differential variables x)
0  = g(x, y)        (algebraic variables y, constrained to the manifold g = 0)
```

The **index** is (roughly) how many times `g` must be differentiated to recover an
explicit ODE for `y`. Index-1 (the common case, `∂g/∂y` invertible) is the
tractable target; higher-index DAEs need index reduction first.

## Design reasoning (mine, not CAPD's): DAEs fit dReal's *existing* machinery

The key observation is that a DAE is exactly **an ODE plus nonlinear algebraic
constraints**, and dReal already has rigorous contractors for **both** halves —
so the pieces it needs are mostly already present, just not wired for DAEs:

| DAE need | dReal already has |
|---|---|
| integrate the differential part `x'=f(x,y)` | the CAPD ODE contractor (`contractor_odes_capd.cc`) — see [../dreal-capd-usage.md](../dreal-capd-usage.md) |
| enforce the algebraic constraint `g(x,y)=0` on the trajectory | ibex HC4 nonlinear-constraint contractors — and specifically the **per-slice `forall_t` invariant check** already runs a constraint on every tube slice (`contractor_odes.cc`) |
| consistent initial conditions (`g(x₀,y₀)=0` + hidden constraints) | ICP narrowing against the constraint contractors finds consistent ICs automatically |

So the most promising route is **not** a new integrator but a composition of what
exists:

1. **Treat the algebraic constraint as a trajectory invariant.** `g(x,y)=0` over
   the whole horizon is structurally identical to a `forall_t` invariant that must
   hold on every slice — except it is an *equality* that also **contracts `y`**
   (solve `g(x,y)=0` for `y` given the slice's `x`-enclosure via the existing HC4
   contractor). This is the natural extension point: the per-slice loop already
   writes each slice's state into the box and runs ibex contractors on it.
2. **Index reduction is a dReal-side preprocessing step**, not a CAPD feature. For
   index > 1, differentiate `g` symbolically (dReal already has the Drake symbolic
   layer + automatic differentiation) to expose the hidden constraints, then
   integrate the resulting index-1 system. This is solver-frontend work, separate
   from the enclosure backend.
3. **The drift problem largely dissolves.** A classical numerical DAE integrator
   fights *drift off the constraint manifold*. dReal's per-slice **contraction**
   onto `g=0` actively projects the enclosure back onto the manifold each slice —
   so the constraint is enforced rigorously rather than approximately maintained.

### What would need building (analogous to the ODE contractor)

- A DAE constraint AST node (or a reuse of `Integral` + an algebraic-constraint
  conjunct) so the frontend can express `x'=f(x,y) ∧ g(x,y)=0`.
- In the per-slice filter: after writing slice `x` into the box, run the algebraic
  constraint's HC4 contractor to narrow `y` (and refute slices where `g=0` is
  infeasible) — a small generalization of the current invariant check.
- Index detection / reduction at the frontend (symbolic differentiation of `g`),
  with an explicit error if the index exceeds what's implemented (no silent
  fallback).

> Caveat (source fidelity): the routes above are a *design sketch* grounded in
> dReal's current architecture, not a validated implementation and not sourced
> from CAPD. The one hard fact is the heading: **CAPD provides no DAE primitives**,
> so any DAE support is dReal-side composition of the ODE backend with the existing
> nonlinear-constraint contractors. Whether the per-slice-contraction approach is
> tight/fast enough is unmeasured.

## Source

CAPD DAE support: **none** (verified absent in docs + headers). Related CAPD
capabilities dReal would build *on top of*:
[../modules/dynsys.md](../modules/dynsys.md) (ODE integration),
[../concepts/timemap-integration.md](timemap-integration.md),
and dReal's own constraint layer (ibex HC4, the `forall_t` per-slice check).
