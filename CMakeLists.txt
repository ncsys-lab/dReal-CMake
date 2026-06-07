cmake_minimum_required(VERSION 3.16)
project(dreal4_cmake)
include(CMakePrintHelpers)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

# NOTE: CMAKE_OSX_ARCHITECTURES is no longer forced to x86_64.
# CAPD (the original x86-only ODE library) has been replaced with Codac, which
# supports ARM64 natively. If you need to override (e.g. for cross-compilation),
# pass -DCMAKE_OSX_ARCHITECTURES=x86_64 on the cmake command line.

if (CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
    function(find_brew_package package_name variable_name)
        # Try ARM homebrew first, then x86 homebrew
        foreach(BREW_EXEC /opt/homebrew/bin/brew /usr/local/bin/brew)
            if (EXISTS "${BREW_EXEC}")
                execute_process(
                        COMMAND "${BREW_EXEC}" --prefix ${package_name}
                        RESULT_VARIABLE BREW_RESULT
                        OUTPUT_VARIABLE BREW_PREFIX
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                )
                if (BREW_RESULT EQUAL 0 AND EXISTS "${BREW_PREFIX}")
                    message(STATUS "Found ${package_name} via Homebrew at ${BREW_PREFIX}")
                    if (NOT DEFINED ${variable_name})
                        set(${variable_name} "${BREW_PREFIX}" PARENT_SCOPE)
                    endif()
                    return()
                endif()
            endif()
        endforeach()
    endfunction()

    find_brew_package(bison BISON_ROOT)
    find_brew_package(flex FLEX_ROOT)
    find_brew_package(gmp GMP_ROOT)
    find_brew_package(cadical CADICAL_ROOT)
    find_brew_package(eigen EIGEN3_ROOT)
endif ()


# parsers
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(PARSERS_HOME "${CMAKE_CURRENT_BINARY_DIR}/parsers/")
file(MAKE_DIRECTORY "${PARSERS_HOME}/dreal/smt2/")
FLEX_TARGET(smt2_scanner "${PROJECT_SOURCE_DIR}/src/dreal/smt2/scanner.ll" "${PARSERS_HOME}/dreal/smt2/scanner.ll.cc")
BISON_TARGET(smt2_parser "${PROJECT_SOURCE_DIR}/src/dreal/smt2/parser.yy" "${PARSERS_HOME}/dreal/smt2/parser.yy.cc")
ADD_FLEX_BISON_DEPENDENCY(smt2_scanner smt2_parser)

file(MAKE_DIRECTORY "${PARSERS_HOME}/dreal/dr/")
FLEX_TARGET(dr_scanner "${PROJECT_SOURCE_DIR}/src/dreal/dr/scanner.ll" "${PARSERS_HOME}/dreal/dr/scanner.ll.cc")
BISON_TARGET(dr_parser "${PROJECT_SOURCE_DIR}/src/dreal/dr/parser.yy" "${PARSERS_HOME}/dreal/dr/parser.yy.cc")
ADD_FLEX_BISON_DEPENDENCY(dr_scanner dr_parser)


# manually imported dependencies
set(DRAKE_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_robotlocomotion_drake/")
set(DYNBITSET_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_pinam45_dynamic_bitset/")
set(THREADPOOL_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_progschj_threadpool/")
set(EZPARSER_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_dreal-deps_ezoptionparser/")
set(LIBCDS_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_khizmax_libcds/")
set(JSON_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_nlohmann_json/")
set(PICOSAT_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_dreal-deps_picosat/")

unset(DREAL_SOURCES)
unset(DREAL_SOURCES CACHE)
file(GLOB_RECURSE DREAL_SOURCES
        "${PROJECT_SOURCE_DIR}/src/dreal/*/**.cc"
        "${PROJECT_SOURCE_DIR}/src/dreal/*/**.yy"
        "${PROJECT_SOURCE_DIR}/src/dreal/*/**.ll"
        "${DRAKE_HOME}/**.cc"
        "${THREADPOOL_HOME}/**.cc"
        "${LIBCDS_HOME}/src/**.cpp"
        "${PICOSAT_HOME}/src/**.c"
)
add_library(dreal4_cmake STATIC
        ${DREAL_SOURCES}
        ${BISON_smt2_parser_OUTPUTS} ${FLEX_smt2_scanner_OUTPUTS}
        ${BISON_dr_parser_OUTPUTS} ${FLEX_dr_scanner_OUTPUTS}
)
target_include_directories(dreal4_cmake PUBLIC
        "${PARSERS_HOME}"
        "${PROJECT_SOURCE_DIR}/src/"
        "${DRAKE_HOME}"
        "${DYNBITSET_HOME}"
        "${THREADPOOL_HOME}"
        "${EZPARSER_HOME}"
        "${LIBCDS_HOME}"
        "${JSON_HOME}"
        "${PICOSAT_HOME}/src/"
)

# system dependencies
find_library(   FLEX_LIB NAMES libfl.a    HINTS "${FLEX_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${FLEX_INCLUDE_DIRS}")
target_link_libraries(dreal4_cmake PUBLIC "${FLEX_LIB}")

find_path(       GMP_INC NAMES gmpxx.h    HINTS "${GMP_ROOT}" PATH_SUFFIXES include includes     REQUIRED NO_CACHE)
find_library(  GMP_C_LIB NAMES libgmp.a   HINTS "${GMP_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
find_library(GMP_CXX_LIB NAMES libgmpxx.a HINTS "${GMP_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${GMP_INC}")
target_link_libraries(dreal4_cmake PUBLIC "${GMP_C_LIB}" "${GMP_CXX_LIB}")

find_path(   CADICAL_INC NAMES cadical.hpp  HINTS "${CADICAL_ROOT}" PATH_SUFFIXES include includes     REQUIRED NO_CACHE)
find_library(CADICAL_LIB NAMES libcadical.a HINTS "${CADICAL_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${CADICAL_INC}")
target_link_libraries(dreal4_cmake PUBLIC "${CADICAL_LIB}")

# Eigen3 (required by Codac)
find_package(Eigen3 QUIET NO_MODULE HINTS "${EIGEN3_ROOT}")
if (NOT Eigen3_FOUND)
    # Fall back to searching standard system paths
    find_package(Eigen3 REQUIRED NO_MODULE)
endif()

# ---------------------------------------------------------------------------
# IBEX + Codac via ExternalProject
#
# Codac v2.0.2 requires IBEX to be pre-installed (find_package(IBEX REQUIRED)
# in Codac's own CMakeLists.txt). We use lebarsfa/ibex-lib — the fork that
# ships IBEXConfig.cmake and has ARM64 support, which is what Codac targets.
#
# Build order:  ibex_external → codac_external → dreal4_cmake
#
# On macOS, IBEX's Bison-generated parser hits a std::apply instantiation error
# with Xcode 16+ / libc++ (Darwin 25+). Codac's own CI avoids this by
# downloading pre-built binaries rather than building from source. We do the
# same: detect the Darwin version and arch, then fetch the matching official
# release zip. On Linux, we build from source as normal.
#
# IMPORTED library targets point at the install paths so CMake can establish
# the correct build ordering via add_dependencies, even though ExternalProjects
# build at build-time, not configure-time.
# ---------------------------------------------------------------------------
include(ExternalProject)

set(IBEX_INSTALL_DIR  "${CMAKE_BINARY_DIR}/ibex-install")
set(CODAC_INSTALL_DIR "${CMAKE_BINARY_DIR}/codac-install")

set(EP_ARCH_ARGS "")
if (DEFINED CMAKE_OSX_ARCHITECTURES)
    list(APPEND EP_ARCH_ARGS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
endif()

if (APPLE)
    # Detect arch + macOS version to pick the right pre-built zip.
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(_EP_ARCH "arm64")
    else()
        set(_EP_ARCH "x86_64")
    endif()
    string(REGEX MATCH "^([0-9]+)" _DARWIN_MAJOR "${CMAKE_SYSTEM_VERSION}")
    if (_DARWIN_MAJOR GREATER_EQUAL 25)
        set(_MACOS_NAME "tahoe")
    elseif (_DARWIN_MAJOR GREATER_EQUAL 24)
        set(_MACOS_NAME "sequoia")
    elseif (_DARWIN_MAJOR GREATER_EQUAL 23)
        set(_MACOS_NAME "sonoma")
    else()
        set(_MACOS_NAME "ventura")
    endif()
    message(STATUS "Using pre-built IBEX/Codac for ${_EP_ARCH}_${_MACOS_NAME}")

    # Download at configure time so .a files exist before Ninja runs.
    # file(ARCHIVE_EXTRACT) does NOT strip the top-level directory, so after
    # extracting ibex_arm64_tahoe.zip to ibex_extract/ the files sit under
    # ibex_extract/ibex/include, ibex_extract/ibex/lib, etc.

    if (NOT EXISTS "${IBEX_INSTALL_DIR}/lib/libibex.a")
        message(STATUS "Downloading IBEX pre-built binary for ${_EP_ARCH}_${_MACOS_NAME}...")
        set(_IBEX_ZIP "${CMAKE_BINARY_DIR}/ibex_dl.zip")
        file(DOWNLOAD
            "https://github.com/lebarsfa/ibex-lib/releases/download/ibex-2.8.9.20250626/ibex_${_EP_ARCH}_${_MACOS_NAME}.zip"
            "${_IBEX_ZIP}" SHOW_PROGRESS TLS_VERIFY ON)
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/ibex_extract")
        file(ARCHIVE_EXTRACT INPUT "${_IBEX_ZIP}" DESTINATION "${CMAKE_BINARY_DIR}/ibex_extract")
        file(GLOB _ibex_extracted "${CMAKE_BINARY_DIR}/ibex_extract/*")
        list(LENGTH _ibex_extracted _ibex_count)
        if (_ibex_count EQUAL 1)
            file(RENAME "${_ibex_extracted}" "${IBEX_INSTALL_DIR}")
        else()
            file(RENAME "${CMAKE_BINARY_DIR}/ibex_extract" "${IBEX_INSTALL_DIR}")
        endif()
        file(REMOVE "${_IBEX_ZIP}")
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/ibex_extract")
    endif()

    if (NOT EXISTS "${CODAC_INSTALL_DIR}/lib/libcodac-core.a")
        message(STATUS "Downloading Codac pre-built binary for ${_EP_ARCH}_${_MACOS_NAME}...")
        set(_CODAC_ZIP "${CMAKE_BINARY_DIR}/codac_dl.zip")
        file(DOWNLOAD
            "https://github.com/codac-team/codac/releases/download/v2.0.2/codac_${_EP_ARCH}_${_MACOS_NAME}.zip"
            "${_CODAC_ZIP}" SHOW_PROGRESS TLS_VERIFY ON)
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/codac_extract")
        file(ARCHIVE_EXTRACT INPUT "${_CODAC_ZIP}" DESTINATION "${CMAKE_BINARY_DIR}/codac_extract")
        file(GLOB _codac_extracted "${CMAKE_BINARY_DIR}/codac_extract/*")
        list(LENGTH _codac_extracted _codac_count)
        if (_codac_count EQUAL 1)
            file(RENAME "${_codac_extracted}" "${CODAC_INSTALL_DIR}")
        else()
            file(RENAME "${CMAKE_BINARY_DIR}/codac_extract" "${CODAC_INSTALL_DIR}")
        endif()
        file(REMOVE "${_CODAC_ZIP}")
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/codac_extract")
    endif()
else()
    # Linux: configure-time download of pre-built noble binaries.
    # Building IBEX from source on Linux hits a std::apply ADL conflict in IBEX's
    # Bison-generated parser when compiled against GCC 13 / libstdc++ 13 (Ubuntu 24.04).
    # Pre-built binaries avoid this and also drastically speed up the build.
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
        set(_LINUX_ARCH "amd64")
    elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(_LINUX_ARCH "arm64")
    else()
        message(FATAL_ERROR "No pre-built IBEX/Codac for Linux arch '${CMAKE_SYSTEM_PROCESSOR}'. "
                            "Add source-build support in CMakeLists.txt or cross-compile.")
    endif()
    message(STATUS "Using pre-built IBEX/Codac noble binaries for ${_LINUX_ARCH}")

    if (NOT EXISTS "${IBEX_INSTALL_DIR}/lib/libibex.a")
        message(STATUS "Downloading IBEX pre-built binary for ${_LINUX_ARCH}_noble...")
        set(_IBEX_ZIP "${CMAKE_BINARY_DIR}/ibex_dl.zip")
        file(DOWNLOAD
            "https://github.com/lebarsfa/ibex-lib/releases/download/ibex-2.8.9.20250626/ibex_${_LINUX_ARCH}_noble.zip"
            "${_IBEX_ZIP}" SHOW_PROGRESS TLS_VERIFY ON)
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/ibex_extract")
        file(ARCHIVE_EXTRACT INPUT "${_IBEX_ZIP}" DESTINATION "${CMAKE_BINARY_DIR}/ibex_extract")
        file(GLOB _ibex_extracted "${CMAKE_BINARY_DIR}/ibex_extract/*")
        list(LENGTH _ibex_extracted _ibex_count)
        if (_ibex_count EQUAL 1)
            file(RENAME "${_ibex_extracted}" "${IBEX_INSTALL_DIR}")
        else()
            file(RENAME "${CMAKE_BINARY_DIR}/ibex_extract" "${IBEX_INSTALL_DIR}")
        endif()
        file(REMOVE "${_IBEX_ZIP}")
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/ibex_extract")
    endif()

    if (NOT EXISTS "${CODAC_INSTALL_DIR}/lib/libcodac-core.a")
        message(STATUS "Downloading Codac pre-built binary for ${_LINUX_ARCH}_noble...")
        set(_CODAC_ZIP "${CMAKE_BINARY_DIR}/codac_dl.zip")
        file(DOWNLOAD
            "https://github.com/codac-team/codac/releases/download/v2.0.2/codac_${_LINUX_ARCH}_noble.zip"
            "${_CODAC_ZIP}" SHOW_PROGRESS TLS_VERIFY ON)
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/codac_extract")
        file(ARCHIVE_EXTRACT INPUT "${_CODAC_ZIP}" DESTINATION "${CMAKE_BINARY_DIR}/codac_extract")
        file(GLOB _codac_extracted "${CMAKE_BINARY_DIR}/codac_extract/*")
        list(LENGTH _codac_extracted _codac_count)
        if (_codac_count EQUAL 1)
            file(RENAME "${_codac_extracted}" "${CODAC_INSTALL_DIR}")
        else()
            file(RENAME "${CMAKE_BINARY_DIR}/codac_extract" "${CODAC_INSTALL_DIR}")
        endif()
        file(REMOVE "${_CODAC_ZIP}")
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/codac_extract")
    endif()
endif()

# ---- IMPORTED targets for IBEX ----
# gaol and ultim are IBEX's interval arithmetic backends, installed under lib/ibex/3rd/.
add_library(ibex_imported STATIC IMPORTED GLOBAL)
set_target_properties(ibex_imported PROPERTIES
    IMPORTED_LOCATION "${IBEX_INSTALL_DIR}/lib/libibex.a"
    INTERFACE_INCLUDE_DIRECTORIES "${IBEX_INSTALL_DIR}/include"
)
add_library(ibex_gaol  STATIC IMPORTED GLOBAL)
add_library(ibex_ultim STATIC IMPORTED GLOBAL)
set_target_properties(ibex_gaol  PROPERTIES IMPORTED_LOCATION "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libgaol.a")
set_target_properties(ibex_ultim PROPERTIES IMPORTED_LOCATION "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libultim.a")

# ---- IMPORTED target for Codac ----
# Codac v2 installs as libcodac-core.a (core ICP/contractors) plus
# libcodac-graphics.a (visualization). Only core is needed by dreal4.
add_library(codac_imported STATIC IMPORTED GLOBAL)
set_target_properties(codac_imported PROPERTIES
    IMPORTED_LOCATION "${CODAC_INSTALL_DIR}/lib/libcodac-core.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CODAC_INSTALL_DIR}/include"
)

# ---------------------------------------------------------------------------
# CAPD via ExternalProject — second ODE backend (order-20 Taylor) running
# alongside Codac's order-2 CtcLohner. See CODAC_MIGRATION.md for the
# motivation and gating policy.
#
# The historical FILIB-on-ARM64 blocker that abandoned the prior CAPD attempt
# is gone in CAPD master: CAPD_INTERVAL_TYPE=NATIVE makes capdExt skip
# add_subdirectory(filibsrc) entirely and CAPD's own DoubleRounding (ARM64
# `msr fpcr` assembly, x86 SSE) is used instead. The two-file FILIB-bypass
# patch documented in CODAC_MIGRATION.md is no longer required.
#
# Pin: b353e170 (2026-05-18). The CAPD_INTERVAL_TYPE switch landed in the
# v6.1.0 development cycle (Jan 26, 2025 — see CAPD PR #22 / commit b7506bf9
# bumping the version, alongside the CMAKE_HOST_SYSTEM_PROCESSOR fix). No
# v6.1.0 release tag exists yet, so we pin a master SHA. Bump only after
# verifying that the CAPD_INTERVAL_TYPE plumbing is still intact.
# ---------------------------------------------------------------------------
set(CAPD_INSTALL_DIR "${CMAKE_BINARY_DIR}/capd-install")
set(CAPD_GIT_TAG     "b353e170")

ExternalProject_Add(capd_external
    PREFIX            "${CMAKE_BINARY_DIR}/capd_ep"
    GIT_REPOSITORY    "https://github.com/CAPDGroup/CAPD.git"
    GIT_TAG           "${CAPD_GIT_TAG}"
    GIT_SHALLOW       OFF
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=${CAPD_INSTALL_DIR}"
        "-DCAPD_INTERVAL_TYPE=NATIVE"
        "-DCAPD_ENABLE_MULTIPRECISION=OFF"
        "-DCAPD_BUILD_ALL=OFF"
        "-DCAPD_BUILD_TESTS=OFF"
        "-DCMAKE_CXX_STANDARD=17"
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        "-DCMAKE_BUILD_TYPE=Release"
        ${EP_ARCH_ARGS}
    BUILD_BYPRODUCTS  "${CAPD_INSTALL_DIR}/lib/libcapd.a"
    INSTALL_DIR       "${CAPD_INSTALL_DIR}"
)

# IMPORTED target — the .a file does not exist until build time, but CMake
# only checks existence at link time, so add_dependencies wires the order.
file(MAKE_DIRECTORY "${CAPD_INSTALL_DIR}/include")
add_library(capd_imported STATIC IMPORTED GLOBAL)
add_dependencies(capd_imported capd_external)
set_target_properties(capd_imported PROPERTIES
    IMPORTED_LOCATION "${CAPD_INSTALL_DIR}/lib/libcapd.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CAPD_INSTALL_DIR}/include"
    # CAPD's interval/lib.h fails with "No interval type selected!" unless
    # exactly one of __USE_NATIVE__ / __USE_FILIB__ / __USE_CXSC__ is
    # defined on the consumer's compile line. We built CAPD with
    # CAPD_INTERVAL_TYPE=NATIVE, so consumers must define __USE_NATIVE__
    # to pick the matching template instantiations.
    INTERFACE_COMPILE_DEFINITIONS "__USE_NATIVE__"
)

# All platforms now use configure-time downloads, so .a files already exist
# by the time CMake validates IMPORTED targets — no add_dependencies needed
# for IBEX/Codac. CAPD is the exception: it builds at build time.

# Wire dreal4_cmake against Codac, IBEX, and CAPD (static linking requires explicit list).
target_include_directories(dreal4_cmake PUBLIC
    "${IBEX_INSTALL_DIR}/include"
    "${IBEX_INSTALL_DIR}/include/ibex"
    "${IBEX_INSTALL_DIR}/include/ibex/3rd"
    "${CODAC_INSTALL_DIR}/include"
    "${CODAC_INSTALL_DIR}/include/codac-core"
    "${CAPD_INSTALL_DIR}/include"
)
# contractor_odes_codac.cc includes Codac v2 headers which require C++20 (concepts,
# std::numbers).  All other translation units stay at C++17.
# CAPD requires C++17 only — same as the rest of the project.
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/dreal/contractor/odes/contractor_odes_codac.cc"
    PROPERTIES COMPILE_OPTIONS "-std=c++20"
)

target_link_libraries(dreal4_cmake PUBLIC
    codac_imported
    ibex_imported
    ibex_gaol
    ibex_ultim
    capd_imported
    Eigen3::Eigen
)
# CAPD builds at build time (ExternalProject), so dreal4_cmake must wait for it.
add_dependencies(dreal4_cmake capd_external)

# ---------------------------------------------------------------------------
# FetchContent dependencies (unchanged from before)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt
        GIT_TAG 11.0.2) # version must be kept in sync with what SPDLOG expects
FetchContent_MakeAvailable(fmt)

set(FMT_ROOT "${fmt_BINARY_DIR}")
set(SPDLOG_FMT_EXTERNAL    ON  CACHE BOOL "Use external linked fmt library" FORCE)
set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "Use external header fmt library" FORCE)
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog/
        GIT_TAG v1.15.0)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
        nlopt
        GIT_REPOSITORY https://github.com/stevengj/nlopt
        GIT_TAG v2.9.1)
FetchContent_MakeAvailable(nlopt)

target_link_libraries(dreal4_cmake PUBLIC nlopt spdlog::spdlog fmt::fmt)

# On some platforms, linking is purely sequential; repeat GMP so it resolves on Ubuntu.
target_link_libraries(dreal4_cmake PUBLIC "${GMP_C_LIB}" "${GMP_CXX_LIB}")

# executable
add_executable(dreal4 "${PROJECT_SOURCE_DIR}/src/dreal/dreal_main.cc")
target_link_libraries(dreal4 PRIVATE dreal4_cmake)


# tests
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

set(DRAKE_TEST_HOME "${PROJECT_SOURCE_DIR}/test/third_party/com_github_robotlocomotion_drake/")
file(GLOB_RECURSE DREAL_TESTS
        "${PROJECT_SOURCE_DIR}/test/dreal/**.cc"
        "${DRAKE_TEST_HOME}/**.cc"
)
list(REMOVE_ITEM DREAL_TESTS "${PROJECT_SOURCE_DIR}/test/dreal/api/test/cav19_benchmark.cc")
add_executable(dreal4_cmake_test ${DREAL_TESTS})
target_include_directories(dreal4_cmake_test PRIVATE
        "${PROJECT_SOURCE_DIR}/test/"
        "${DRAKE_TEST_HOME}"
)
target_link_libraries(
        dreal4_cmake_test
        PRIVATE
        dreal4_cmake
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(dreal4_cmake_test)
