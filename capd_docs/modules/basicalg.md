# basicalg (module)

## What it is
Low-level scalar helpers shared across CAPD: compile-time combinatorics, scalar power/square,
and simple statistics, in `capd` / `capd::basicalg` (`basicalg/power.h`, `factrial.cpp`,
`TypeTraits.h`, `doubleFun.h`).

## Key API (from group__basicalg.html)
- Compile-time templates: `Binomial<N,K>`, `Factorial<N>`, `logtwo<N>`, `powerThree<N>`.
- `double realFactorial(unsigned n)`.
- Scalar `power(value, int exponent)` and `sqr(x)` overloaded for `double`/`long double`/`float`/`int`.
- Statistics: `average(beg,end)`, `linearRegression(x,y)`, `powerRegression(x,y)`.
- `TypeTraits<T>::epsilon()` (used as default tolerance throughout matrixAlgorithms) and
  `doubleFun.h` "double versions" of interval functions (`leftBound`/`rightBound`/`mid` for plain
  doubles, so generic code compiles over both scalar and interval types).

## dReal status
**Unused (directly).** Not on dReal's referenced-symbol list (`dreal-capd-usage.md`). These are
internal utilities; `TypeTraits`/`doubleFun` are pulled in transitively by the vectalg and
matrixAlgorithms templates dReal's CAPD dependency compiles, but dReal calls none of them.

## Why it might matter
`TypeTraits<T>::epsilon()` is the default relative tolerance that CAPD's eigenvalue/matrixExp
routines fall back to — relevant only if dReal ever enables C1/Cn integration. `doubleFun.h` is
the genericity trick that lets CAPD's interval algorithms also run in plain `double`; not a dReal
lever.

## Source
[group__basicalg.html](../../../CAPD/docs/html/group__basicalg.html)
