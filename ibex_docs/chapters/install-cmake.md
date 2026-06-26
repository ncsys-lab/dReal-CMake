# Chapter: Install (CMake)

Source: [`install-cmake.rst.txt`](../../../ibex-docs/_sources/install-cmake.rst.txt)
· `install-cmake.html`. How to build IBEX with CMake, incl. the `INTERVAL_LIB` and
`LP_LIB` choices.

> **dReal status:** dReal does **not** install IBEX separately — it source-builds
> the fork via `FetchContent`/ExternalProject (`CMakeLists.txt:195`, pinned sha
> `d9930909`) with **two deliberate flags**:
> - `-DINTERVAL_LIB=gaol` — gaol arithmetic (the patched backend; arm64-native).
> - `-DLP_LIB=none` — *no LP solver*. Comment: "dReal does not exercise IBEX's
>   LP-based contractors."

The `LP_LIB=none` choice is exactly why the [polytope hull](contractor.md) path is
dormant (see [`../dreal-ibex-usage.md`](../dreal-ibex-usage.md)): reviving it means
flipping this to Soplex or CLP. The interval-lib choice (gaol) is the subject of
fork patches #8–#12 (rounding + soundness). Both flags are the two build-time
"knobs" in [`../KNOBS.md`](../KNOBS.md).
