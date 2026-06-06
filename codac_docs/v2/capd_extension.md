# Codac v2 — `codac-capd` extension

Sources:
- https://codac.io/manual/extensions/capd/capd.html (the extension overview)
- https://codac.io/manual/extensions/capd/peibos_capd.html (PEIBOS reach-set)
- https://github.com/codac-team/codac/tree/codac2/src/extensions/capd (source)
- `find_package(CAPD REQUIRED)` block in Codac root `CMakeLists.txt`

## What the extension actually is

**A type-conversion shim**, not a contractor. There is no `CtcCapd` class. The full public API (`codac2_capd.h`):

```cpp
// Scalars / vectors / matrices
capd::Interval        to_capd(const codac2::Interval&);
codac2::Interval      to_codac(const capd::Interval&);
capd::IVector         to_capd(const codac2::IntervalVector&);
codac2::IntervalVector to_codac(const capd::IVector&);
capd::IMatrix         to_capd(const codac2::IntervalMatrix&);
codac2::IntervalMatrix to_codac(const capd::IMatrix&);

// The one piece that talks to integration:
codac2::SlicedTube<codac2::IntervalVector>
  to_codac(const codac2::SolutionCurveWrapper&,
           const std::shared_ptr<TDomain>&);
```

`SolutionCurveWrapper` (`codac2_SolutionCurveWrapper.h`) is just an inheritance wrapper around `capd::ITimeMap::SolutionCurve`:

```cpp
class SolutionCurveWrapper : public capd::ITimeMap::SolutionCurve {
public:
  SolutionCurveWrapper(const capd::ITimeMap::SolutionCurve& base);
  std::vector<capd::Interval> get_domain() const;
  double t0() const;
  double tf() const;
  int dimension() const;
};
```

## What this means in practice

To use CAPD via this extension, **we still have to write all the CAPD integration code ourselves**:

```cpp
// 1. Build capd::IMap from RHS (we'd write our own dReal Expression → IMap converter)
capd::IMap vf("var:x,v;fun:v,-9.8;");

// 2. Set up CAPD's integrator (this is what gets us order-20 Taylor)
capd::IOdeSolver solver(vf, /*order=*/20);
capd::ITimeMap timeMap(solver);
capd::C0Rect2Set X0_set(to_capd(X0));

// 3. Run CAPD forward integration
auto curve = timeMap(t_ub, X0_set);

// 4. Convert CAPD result → Codac tube using the extension
codac2::SolutionCurveWrapper wrapper(curve);
auto tube = to_codac(wrapper, tdomain);

// 5. Do whatever forward-backward narrowing logic we need on the tube
```

Steps 1, 2, 3, 5 are entirely on us. The extension only saves us writing step 4 by hand (≈ 30–50 lines of slice-by-slice copy).

## The CAPD installation problem is unchanged

The extension's doc page is explicit:

> "you first need to install the CAPD library. You can find the installation instructions on the CAPD website"

`find_package(CAPD REQUIRED)` means Codac assumes CAPD is already installed. No ExternalProject, no FILIB management. **The ARM64 / FILIB blocker documented in `CODAC_MIGRATION.md` § "CAPD v6 Direct Integration Attempt" is exactly the same here.** The two-file FILIB-bypass patch is still required.

## And Codac has to be rebuilt from source

Our current build downloads pre-built Codac v2.0.2 archives that were built with `WITH_CAPD=OFF`. To enable the extension we need:

1. ExternalProject_Add the Codac source with `-DWITH_CAPD=ON`
2. ExternalProject_Add CAPD v6 with the FILIB-bypass patch as `PATCH_COMMAND`
3. CAPD must build before Codac (because Codac's `find_package(CAPD REQUIRED)` runs at Codac's configure time)

I.e. we lose the convenience of the pre-built Codac archives — back to a source build (the original Phase 1 plan from `CODAC_MIGRATION.md`).

## Build dependencies (from `src/extensions/capd/CMakeLists.txt`)

```cmake
add_library(${PROJECT_NAME}-capd ...)
target_link_libraries(${PROJECT_NAME}-capd PUBLIC
  ${PROJECT_NAME}-core
  Ibex::ibex
  Eigen3::Eigen
  capd::capd)
set(CMAKE_CXX_STANDARD 20)
```

So C++20 (matches our existing `contractor_odes_codac.cc`), needs `capd::capd` target available, and creates a separate library `codac-capd` that we'd link against alongside `codac-core`.

## PEIBOS-CAPD — not what we need

PEIBOS-CAPD is a **reach-set** computation tool, not a contractor. From the doc:

> "PEIBOS-CAPD is not a contractor for solving differential equations. Instead, it uses two sequential functions: PEIBOS() ... computes a box containing x̄(t) and an interval matrix enclosing the Jacobian matrix ... reach_set() converts the integration results into geometric approximations."

It's the wrong shape for SMT contraction. Reach-set gives "where could the trajectory be" without using a target gate. We need the opposite: given Xt at t_ub, narrow X_0 (and vice versa). Useless for dReal's ICP loop.

## Net assessment

The extension is a **cleaner integration**, not a solved problem.

| What we hoped | What we get |
|---|---|
| A drop-in CtcCapd contractor swap for CtcLohner | Type conversions only — we still write the contractor |
| CAPD bundled / managed by Codac | CAPD must be installed separately; ARM64 FILIB problem unchanged |
| Order-20 Taylor exposed via a simple API | Order is set on `capd::IOdeSolver`; that part is unchanged from raw CAPD |
| Avoid the abandoned direct-CAPD attempt | Same code as that attempt, plus extra build coordination for Codac+WITH_CAPD |

Net delta vs. the abandoned direct-CAPD attempt:
- **Saves**: 30–50 LoC of `SolutionCurve` → tube conversion
- **Costs**: switching our build from pre-built Codac archives to source-built Codac; adding a second ExternalProject (CAPD itself with FILIB patch)
- **Doesn't change**: FILIB ARM64 patch, CAPD install steps, CAPD-side integration code (`IOdeSolver`/`ITimeMap`/`IMap`), forward-backward propagation logic on top of the tube
