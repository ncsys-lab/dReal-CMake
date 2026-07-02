# `LinearizerXTaylor` — corner-based interval Taylor relaxation

> ⚠ **Stale "dormant" status** (pre-`fa3b74bd7`) — corrected 2026-07-02: the polytope path this
> linearizer feeds is **live** via `--polytope` now that IBEX builds `-DLP_LIB=soplex`
> (`CMakeLists.txt:204`). The "`--polytope` off, `LP_LIB=none`, knobs unexposed/unexplored" note below
> is superseded. See [`README.md`](../../README.md) top banner.

Header:
[`ibex_LinearizerXTaylor.h`](../../../../ibex-fork/src/numeric/ibex_LinearizerXTaylor.h)
(`numeric/`). `class LinearizerXTaylor : public Linearizer`. The linear relaxation
dReal feeds to [`CtcPolytopeHull`](../contractors/CtcPolytopeHull.md) (Araya,
Trombettoni, Neveu, CPAIOR 2012).

> **dReal status:** constructed at library defaults
> (`contractor_ibex_polytope.cc:108`), but the polytope path is **dormant**
> (`--polytope` off, `LP_LIB=none`). The knobs below are *unexposed and unexplored*
> — relevant only if the polytope path is revived (audit D).

## Constructor + the option menu (verbatim from the header)

```cpp
LinearizerXTaylor(const System& sys,
                  approx_mode mode    = RELAX,
                  corner_policy corners = RANDOM_OPP,
                  slope_formula slope = HANSEN);

enum approx_mode   { RELAX, RESTRICT };
enum corner_policy { INF, SUP, RANDOM, RANDOM_OPP };
enum slope_formula { TAYLOR, HANSEN };
```

| Knob | Options | Default | Meaning |
|---|---|---|---|
| `mode` | `RELAX` / `RESTRICT` | RELAX | outer relaxation (sound enclosure) vs inner restriction |
| `corners` | `INF`, `SUP`, `RANDOM`, `RANDOM_OPP` | RANDOM_OPP | which box corner(s) to Taylor-expand from. `RANDOM_OPP` = a random point **and** its opposite (2 corners) |
| `slope` | `TAYLOR` / `HANSEN` | HANSEN | slope matrix: plain interval Taylor vs the thinner Hansen slope |

dReal uses `(RELAX, RANDOM_OPP, HANSEN)` — i.e. the library's own recommended
defaults (HANSEN slope is tighter than TAYLOR; RANDOM_OPP uses 2 corners).

## Other linearizers in the fork (`numeric/`)

- `LinearizerCompo` — intersect several linearizers' polytopes.
- `LinearizerFixed` — a fixed `Ax≤b`.
- `LinearizerDuality` — duality-based relaxation.
- **`LinearizerAffine2` is absent** — affine arithmetic lives in the separate
  `ibex-affine` plugin (verified: no `*affine*` source in the fork). Treat as a
  plugin task, not a knob ([`../../AUDIT.md`](../../AUDIT.md) D).

Related: [`../contractors/CtcPolytopeHull.md`](../contractors/CtcPolytopeHull.md),
[contractor chapter](../../chapters/contractor.md).
