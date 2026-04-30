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
# Codac via ExternalProject (brings in IBEX automatically)
#
# Codac is built as an ExternalProject. It will download and build IBEX
# (lebarsfa/ibex-lib) internally as part of its build process. We only need
# to point Codac at system dependencies (GMP, Eigen3).
#
# NOTE: Because ExternalProject targets build at build-time (not configure-
# time), we create IMPORTED library targets that point at the paths where the
# installs will land. These are listed as link libraries and cmake will set
# up the correct build ordering via add_dependencies.
# ---------------------------------------------------------------------------
include(ExternalProject)

set(CODAC_INSTALL_DIR "${CMAKE_BINARY_DIR}/codac-install")

# Create the install directory before setting IMPORTED target properties.
file(MAKE_DIRECTORY "${CODAC_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${CODAC_INSTALL_DIR}/lib")

# Propagate the host architecture to child projects so that ARM64 mac builds
# work end-to-end without manually passing flags.
set(EP_ARCH_ARGS "")
if (DEFINED CMAKE_OSX_ARCHITECTURES)
    list(APPEND EP_ARCH_ARGS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
endif()

# ---- Codac (handles IBEX internally) ----
# Codac provides CtcFunction (HC4 forward-backward) and CtcLohner (ODE integration).
# WITH_CAPD=OFF: Codac has its own rigorous ODE integrator (Lohner method) that does
# not require a separate CAPD installation.
ExternalProject_Add(codac_external
    GIT_REPOSITORY "https://github.com/codac-team/codac.git"
    GIT_TAG        "v2.0.2"
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CODAC_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_STANDARD=17
        -DWITH_CAPD=OFF
        -DBUILD_TESTS=OFF
        -DCMAKE_PREFIX_PATH="${GMP_ROOT};${EIGEN3_ROOT}"
        ${EP_ARCH_ARGS}
    BUILD_COMMAND   cmake --build <BINARY_DIR> -j8
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    LOG_DOWNLOAD  TRUE
    LOG_CONFIGURE TRUE
    LOG_BUILD     TRUE
    LOG_INSTALL   TRUE
)

# ---- IMPORTED target for Codac (and IBEX via Codac) ----
add_library(codac_imported STATIC IMPORTED GLOBAL)
set_target_properties(codac_imported PROPERTIES
    IMPORTED_LOCATION "${CODAC_INSTALL_DIR}/lib/libcodac.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CODAC_INSTALL_DIR}/include"
)
add_dependencies(codac_imported codac_external)

# Wire dreal4_cmake against Codac (which brings in IBEX).
add_dependencies(dreal4_cmake codac_external)
target_include_directories(dreal4_cmake PUBLIC
    "${CODAC_INSTALL_DIR}/include"         # codac headers (which include ibex.h)
)
target_link_libraries(dreal4_cmake PUBLIC
    codac_imported
    Eigen3::Eigen
)

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
