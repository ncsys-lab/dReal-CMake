# DoubleRounding (Rounding)

## What it is
`capd::rounding::DoubleRounding` — the class that switches the FPU directed-rounding mode for
`double`/`long double`, making CAPD interval arithmetic rigorous. It is the default rounding
policy (`T_Rnd`) for double-endpoint `Interval`. The integer counterpart is `IntRounding` (a
no-op, since integer ops are exact).

## Key API (static methods, from `rounding/DoubleRounding.h`)
- `static void roundNearest()` — round to nearest.
- `static void roundUp()` — round toward +∞.
- `static void roundDown()` — round toward −∞.
- `static void roundCut()` — round toward zero.
- `static RoundingMode test()` — returns the current mode.
- `static bool isWorking()` — self-test that mode switching actually takes effect on this
  build/arch (returns false if not).
- `enum RoundingMode { RoundUnknown=-1, RoundNearest, RoundDown, RoundUp, RoundCut }`.

## dReal status
**Underlying mechanism, not called directly.** dReal manages the FPU mode itself via
`UpwardRoundingScope`/`NearestRoundingScope` at ICP-phase granularity (`docs/rounding.md`), and
treats CAPD adapters as sanctioned mode-clobberers (`ExpectClobber`). CAPD interval ops depend on
the ambient mode being correct; dReal re-establishes the right scope around CAPD calls.

## Why it might matter
`isWorking()` is a ready-made guard for the documented native-CAPD-interval risk: under aggressive
GCC optimization the directed-rounding switch can be defeated, producing unsound enclosures
(see `concepts/intervals-and-rounding.md`). A one-call `isWorking()` assertion at startup fits
dReal's "guard all assumptions" discipline and would catch a build where rounding control is
silently broken.

## Source
[group__rounding.html](../../../CAPD/docs/html/group__rounding.html)
