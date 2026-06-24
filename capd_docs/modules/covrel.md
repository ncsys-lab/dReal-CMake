# Module: Covrel (`group__covrel`) — stub

## What it is

`capd::covrel`: **covering relations** and **cones conditions** (Gidea–Zgliczyński), the
topological machinery for computer-assisted proofs of symbolic dynamics, hyperbolicity, and
horseshoes. Provides h-sets (`HSet`, `HSet2D`, `HSet3D`, `HSetMD`, `HSetND`, `TripleSet`),
`QuadraticForm`/`HSetWithCones`, and predicates `inside`/`outside`/`across`/`mapaway`,
`checkCoveringRelation*`, `checkConesCondition`.

## dReal status

**Unused** and not on `dreal-capd-usage.md`'s audit list. This is proof-of-dynamics
infrastructure, orthogonal to dReal's reachability/enclosure use of CAPD.

## Why it might matter

It almost certainly does not. Covering relations prove qualitative statements about a map's
global dynamics (existence of orbits, chaos); dReal needs quantitative interval enclosures of
a flow to a bounded time. No fit to terminal-time gating or parameter uncertainty. Recorded
for completeness only.

## Source

[`../../../CAPD/docs/html/group__covrel.html`](../../../CAPD/docs/html/group__covrel.html)
