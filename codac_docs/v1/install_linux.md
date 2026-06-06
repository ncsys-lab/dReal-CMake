# Codac v1 — Installation (Linux + dependency notes)

Source: https://codac.io/v1/install/01-installation-full-linux.html (fetched 2026-06-05)
Plus: `https://raw.githubusercontent.com/codac-team/codac/codac1/CMakeLists.txt`

## Required dependencies

| Dep | Status | Version | Notes |
|---|---|---|---|
| IBEX | **required** | unspecified (says "uses several features of IBEX library") | Repo points users at `lebarsfa/ibex-lib` (same fork we already use) |
| Eigen3 | **required** | unspecified | `find_package(Eigen3 REQUIRED NO_MODULE)` |
| CMake | **required** | ≥ 3.8 | |
| CAPD | **optional** | — | `option(WITH_CAPD "Using CAPD for accurate integration of ODEs" OFF)` — **OFF by default** |
| C++ standard | — | C++17 | (vs v2's C++20) |

## Implication for our project

Codac v1 with `WITH_CAPD=OFF` (the default) builds with the *same* IBEX fork we already use. No FILIB. No CAPD. No ARM64 platform blocker.

The `WITH_CAPD` option exists for users who want CAPD-grade ODE integration; it's not required for v1 to build, and our v2 build is already running with no CAPD.

## Platform support

- Linux: explicit (amd64 + arm64 binary packages: "Ubuntu (amd64, arm64), Debian (amd64, arm64, armhf)")
- macOS: documentation page at `02-installation-full-macos.html` returned 404 at fetch time; not verified for ARM64 macOS. Source-build expected to work but no published guarantee.

## Build instructions (summary)

```bash
git clone -b master https://github.com/lebarsfa/ibex-lib.git
# build/install IBEX
git clone -b codac1 https://github.com/codac-team/codac.git
# cmake -DCMAKE_INSTALL_PREFIX=... -DWITH_CAPD=OFF; make; make install
```

## Maintenance status of `codac1` branch

- Latest tagged release: **v1.5.7 (March 18, 2024)** — no v1 release in ~2 years.
- Latest commit on `codac1` branch: **January 19, 2026** ("Merge pull request #340 ... Update workflows").
- Net: feature-frozen but CI / build kept current by lebarsfa.
