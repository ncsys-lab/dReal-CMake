# Codac v2 — Local header inventory

From `ls gcc_build/codac-install/include/codac-core/` (snapshot 2026-06-05).

## ODE / tube headers

```
codac2_CtcLohner.h          ← we use this (CtcLohner + LohnerAlgorithm)
codac2_CtcDeriv.h           ← tube-level ẋ = v; doesn't help us
codac2_CtcEval.h            ← y = x(t); doesn't help us
codac2_AnalyticFunction.h   ← we build instances of these from dReal Expressions
codac2_TDomain.h
codac2_SlicedTube.h
codac2_Slice.h
codac2_TimePropag.h         ← FWD | BWD | FWD_BWD enum
```

## Set-theoretic / composition Ctc* headers

```
codac2_Ctc.h                 (base class)
codac2_CtcAction.h
codac2_CtcCartProd.h
codac2_CtcConstell.h
codac2_CtcCross.h
codac2_CtcCtcBoundary.h
codac2_CtcDist.h
codac2_CtcEllipse.h
codac2_CtcEmpty.h
codac2_CtcFixpoint.h
codac2_CtcIdentity.h
codac2_CtcInnerOuter.h
codac2_CtcInter.h
codac2_CtcInverse.h
codac2_CtcInverseNotIn.h
codac2_CtcLazy.h
codac2_CtcNot.h
codac2_CtcNotInside.h
codac2_CtcPointCloud.h
codac2_CtcPolar.h
codac2_CtcPolygon.h
codac2_CtcPolytopeHull.h
codac2_CtcProj.h
codac2_CtcQInter.h
codac2_CtcSegment.h
codac2_CtcTransform.h
codac2_CtcUnion.h
codac2_CtcVisible.h
codac2_CtcWrapper.h
```

## Separator headers (separator ≠ contractor)

```
codac2_SepCtcBoundary.h
codac2_SepCtcPair.h
```

## codac-unsupported package

```
codac-unsupported/codac2_unsupported_empty.h
codac-unsupported/codac-unsupported.h
```

Both are empty stubs. **No v1 contractors are hidden here.**

## Confirmed absent (relative to Codac v1)

- `CtcPicard`
- `CtcDelay`
- `CtcLinobs`
- `CtcChain`
- `CtcStatic` (renamed/restructured)
