# Codac v1 — CMakeLists.txt summary

Source: https://raw.githubusercontent.com/codac-team/codac/codac1/CMakeLists.txt (fetched 2026-06-05)

## Key declarations

```cmake
cmake_minimum_required(VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

## Dependencies

```cmake
find_package(IBEX REQUIRED)
ibex_init_common()

find_package(Eigen3 REQUIRED NO_MODULE)
```

## Options (defaults)

```cmake
option(WITH_PYTHON   "Build Python binding"                                OFF)
option(WITH_CAPD     "Using CAPD for accurate integration of ODEs"        OFF)
option(WITH_TUBE_TREE "Binary trees for fast tube evaluations"            OFF)
option(BUILD_TESTS    "Build test"                                         OFF)
```

`WITH_CAPD=ON` calls:

```cmake
pkg_search_module(PKG_CAPD REQUIRED capd capd-gui mpcapd mpcapd-gui)
```

(CAPD must be already installed; ExternalProject does not build it.)

## Platform notes

Only MSVC-specific block detected:
```cmake
add_definitions(-D_ENABLE_EXTENDED_ALIGNED_STORAGE)
```

No `if(APPLE)` / `if(ARM64)` / `CMAKE_OSX_ARCHITECTURES` blocks in the root CMakeLists. ARM64 macOS would be source-built with default flags.

## Comparison with our current Codac v2 setup

Our CMakeLists pulls a pre-built Codac v2 archive specific to host (Sonoma/Sequoia/Tahoe, ARM64/x86_64). v1 would require building from source via `ExternalProject_Add(codac_external GIT_TAG v1.5.7)` — closer to the original v2-migration plan in `CODAC_MIGRATION.md` Phase 1.
