# ReorganizedSet (and the reorganization policies)

## What it is
`ReorganizedSet<SetT, Reorg>` is a template **adapter** that pairs a dynamical set with a reorganization policy: after every `move`, it calls `reorganizeIfNeeded(set)` (confirmed `dynset_reorganizedset.html`). Reorganization periodically **rebuilds the doubleton representation** before its coordinate frames degrade into near-singularity — the second wrapping defense after per-step QR orthogonalization.

The default reorganization for a given set may be omitted: `ReorganizedSet<C0Rect2Set>` ≡ `ReorganizedSet<C0Rect2Set, C0Rect2Reorganization<...>>`. The facade `C0Rect2RSet` is the reorganizing variant of `C0Rect2Set`.

## The reorganization policies (`dynset/reorganization/`)
- **`FactorReorganization`** — reorganizes when `size(r) > factor · size(r0)` (confirmed `FactorReorganization.h:26-27`). This is the policy baked into dReal's `C0Rect2Policies`.
- **`CanonicalReorganization`** — set `C` and `B` to identity, fold everything into `r0` (confirmed group page).
- **`CoordWiseReorganization`** — the `B`-column for the largest `r` coordinate replaces the closest `C`-column; that error is promoted into `r0`.
- **`SwapReorganization`** — reorganize when `r` is larger than `B⁻¹·C·r0` times a factor (i.e. error frame has overtaken the initial-size frame).
- **`QRReorganization`** — re-orthogonalize `B`.
- **`NoReorganization`** — no-op.

## dReal status
**Used implicitly, not configurable.** dReal does not name `ReorganizedSet`, but its `C0Rect2Policies = FactorReorganization<FullQRWithPivoting<>>` (`typedefs.h:24`) means **every** wired set already reorganizes on the factor trigger. The factor value and the policy choice are **not exposed** by any `--ode-*` flag.

## Why it might matter
For **small initial sets** the dynset narrative explicitly says "reorganization can improve result a lot" and recommends the `R`-suffixed sets. The reorganization *factor* is an unexposed tightness/speed knob: a smaller factor reorganizes more often (tighter frames, more cost). Whether dReal's inherited default factor is right for the ODE benchmark families is untested and a low-risk thing to sweep.

## Source
[dynset_reorganizedset.html](../../../../CAPD/docs/html/dynset_reorganizedset.html) · [group__dynset.html](../../../../CAPD/docs/html/group__dynset.html)
