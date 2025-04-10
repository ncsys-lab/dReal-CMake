cmake_minimum_required(VERSION 3.30)
project(dreal4_cmake)
include(CMakePrintHelpers)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

if (CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
    function(find_brew_package package_name variable_name)
        execute_process(
                COMMAND brew --prefix ${package_name}
                RESULT_VARIABLE BREW_${package_name}
                OUTPUT_VARIABLE BREW_${package_name}_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (NOT DEFINED ${variable_name} AND BREW_${package_name} EQUAL 0 AND EXISTS "${BREW_${package_name}_PREFIX}")
            message(STATUS "Found ${package_name} keg installed by Homebrew at ${BREW_${package_name}_PREFIX}")
            set(${variable_name} "${BREW_${package_name}_PREFIX}" PARENT_SCOPE)
        endif()
    endfunction()

    find_brew_package(bison BISON_ROOT)
    find_brew_package(flex FLEX_ROOT)
    find_brew_package(gmp GMP_ROOT)
    find_brew_package(cadical CADICAL_ROOT)
endif ()


# parsers
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(PARSERS_HOME "${CMAKE_CURRENT_BINARY_DIR}/parsers/")
file(MAKE_DIRECTORY "${PARSERS_HOME}/dreal/smt2/")  # flex crashes on x86 macos if this dir doesn't already exist
FLEX_TARGET(smt2_scanner "${PROJECT_SOURCE_DIR}/src/dreal/smt2/scanner.ll" "${PARSERS_HOME}/dreal/smt2/scanner.ll.cc")
BISON_TARGET(smt2_parser "${PROJECT_SOURCE_DIR}/src/dreal/smt2/parser.yy" "${PARSERS_HOME}/dreal/smt2/parser.yy.cc")
ADD_FLEX_BISON_DEPENDENCY(smt2_scanner smt2_parser)

file(MAKE_DIRECTORY "${PARSERS_HOME}/dreal/dr/")  # flex crashes on x86 macos if this dir doesn't already exist
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
add_library(dreal4_cmake
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
target_include_directories(dreal4_cmake PUBLIC "${FLEX_INCLUDE_DIRS}")
target_link_libraries(dreal4_cmake PUBLIC "${FLEX_LIBRARIES}")

find_path(       GMP_INC NAMES gmpxx.h    HINTS "${GMP_ROOT}" PATH_SUFFIXES include includes     REQUIRED NO_CACHE)
find_library(  GMP_C_LIB NAMES libgmp.a   HINTS "${GMP_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
find_library(GMP_CXX_LIB NAMES libgmpxx.a HINTS "${GMP_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${GMP_INC}")
target_link_libraries(dreal4_cmake PUBLIC "${GMP_C_LIB}" "${GMP_CXX_LIB}")

find_path(   CADICAL_INC NAMES cadical.hpp  HINTS "${CADICAL_ROOT}" PATH_SUFFIXES include includes     REQUIRED NO_CACHE)
find_library(CADICAL_LIB NAMES libcadical.a HINTS "${CADICAL_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${CADICAL_INC}")
target_link_libraries(dreal4_cmake PUBLIC "${CADICAL_LIB}")


# downloaded dependencies
include(FetchContent)

#FetchContent_Declare(
#        fmt
#        GIT_REPOSITORY https://github.com/fmtlib/fmt
#        GIT_TAG 11.1.1)
#FetchContent_MakeAvailable(fmt)

FetchContent_Declare(
        ibex
        GIT_REPOSITORY https://github.com/ncsys-lab/ibex-lib
        GIT_TAG 4d61b84139eaeab628506d9edaf5fd8879a3302e)  # Kunal's M1 + namespace fixes
FetchContent_MakeAvailable(ibex)

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

#FetchContent_Declare(
#        pybind11
#        GIT_REPOSITORY https://github.com/pybind/pybind11
#        GIT_TAG        v2.13.6)
#FetchContent_MakeAvailable(pybind11)
#if(NOT pybind11_POPULATED)
#    # https://stackoverflow.com/questions/47027741/smart-way-to-cmake-a-project-using-pybind11-by-externalproject-add
#    add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR})
#endif()

# ibex's CMake build is a bit flaky... dependency tree isn't correctly set up. This forces it.
add_custom_target(
        GAOL_HACK
        BYPRODUCTS
        "${CMAKE_BINARY_DIR}/_deps/ibex-build/interval_lib_wrapper/gaol/mathlib-2.1.1/lib/libultim.a"
        "${CMAKE_BINARY_DIR}/_deps/ibex-build/interval_lib_wrapper/gaol/gaol-4.2.3/lib/libgaol.a"
        COMMAND exit # no-op
        DEPENDS libultim_3rd libgaol_3rd
)
add_dependencies(dreal4_cmake GAOL_HACK)
target_link_libraries(
        dreal4_cmake PUBLIC ibex nlopt spdlog::spdlog fmt::fmt
)


# executable
add_executable(dreal4 "${PROJECT_SOURCE_DIR}/src/dreal/dreal_main.cc")
target_link_libraries(dreal4 PRIVATE dreal4_cmake)


# tests
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # For Windows: Prevent overriding the parent project's compiler/linker settings
FetchContent_MakeAvailable(googletest)
enable_testing()

set(DRAKE_TEST_HOME "${PROJECT_SOURCE_DIR}/test/third_party/com_github_robotlocomotion_drake/")
file(GLOB_RECURSE DREAL_TESTS
        "${PROJECT_SOURCE_DIR}/test/dreal/**.cc"
        "${DRAKE_TEST_HOME}/**.cc"
)
list(REMOVE_ITEM DREAL_TESTS "${PROJECT_SOURCE_DIR}/test/dreal/api/test/cav19_benchmark.cc")  # main function interferes with gtest
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
