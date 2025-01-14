cmake_minimum_required(VERSION 3.30)
project(dreal4_cmake)
include(CMakePrintHelpers)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# parsers
if (CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
    execute_process(
            COMMAND brew --prefix bison
            RESULT_VARIABLE BREW_BISON
            OUTPUT_VARIABLE BREW_BISON_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT DEFINED BISON_ROOT AND BREW_BISON EQUAL 0 AND EXISTS "${BREW_BISON_PREFIX}")
        message(STATUS "Found Bison keg installed by Homebrew at ${BREW_BISON_PREFIX}")
#        set(BISON_EXECUTABLE "${BREW_BISON_PREFIX}/bin/bison")
        set(BISON_ROOT "${BREW_BISON_PREFIX}/")
    endif ()

    execute_process(
            COMMAND brew --prefix flex
            RESULT_VARIABLE BREW_FLEX
            OUTPUT_VARIABLE BREW_FLEX_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT DEFINED FLEX_ROOT AND BREW_FLEX EQUAL 0 AND EXISTS "${BREW_FLEX_PREFIX}")
        message(STATUS "Found Flex keg installed by Homebrew at ${BREW_FLEX_PREFIX}")
#        set(FLEX_EXECUTABLE "${BREW_FLEX_PREFIX}/bin/flex")
        set(FLEX_ROOT "${BREW_FLEX_PREFIX}/")
    endif ()

    execute_process(
            COMMAND brew --prefix gmp
            RESULT_VARIABLE BREW_GMP
            OUTPUT_VARIABLE BREW_GMP_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT DEFINED GMP_ROOT AND BREW_GMP EQUAL 0 AND EXISTS "${BREW_GMP_PREFIX}")
        message(STATUS "Found GMP keg installed by Homebrew at ${BREW_GMP_PREFIX}")
        set(GMP_ROOT "${BREW_GMP_PREFIX}")
    endif ()

    execute_process(
            COMMAND brew --prefix cadical
            RESULT_VARIABLE BREW_CADICAL
            OUTPUT_VARIABLE BREW_CADICAL_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT DEFINED CADICAL_ROOT AND BREW_CADICAL EQUAL 0 AND EXISTS "${BREW_CADICAL_PREFIX}")
        message(STATUS "Found CADICAL keg installed by Homebrew at ${BREW_CADICAL_PREFIX}")
        set(CADICAL_ROOT "${BREW_CADICAL_PREFIX}")
    endif ()
endif ()

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(PARSERS_HOME "${CMAKE_CURRENT_BINARY_DIR}/parsers/")
FLEX_TARGET(smt2_scanner "${PROJECT_SOURCE_DIR}/src/dreal/smt2/scanner.ll" "${PARSERS_HOME}/dreal/smt2/scanner.ll.cc")
BISON_TARGET(smt2_parser "${PROJECT_SOURCE_DIR}/src/dreal/smt2/parser.yy" "${PARSERS_HOME}/dreal/smt2/parser.yy.cc")
ADD_FLEX_BISON_DEPENDENCY(smt2_scanner smt2_parser)

FLEX_TARGET(dr_scanner "${PROJECT_SOURCE_DIR}/src/dreal/dr/scanner.ll" "${PARSERS_HOME}/dreal/dr/scanner.ll.cc")
BISON_TARGET(dr_parser "${PROJECT_SOURCE_DIR}/src/dreal/dr/parser.yy" "${PARSERS_HOME}/dreal/dr/parser.yy.cc")
ADD_FLEX_BISON_DEPENDENCY(dr_scanner dr_parser)

# manually imported dependencies
set(DRAKE_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_robotlocomotion_drake/")
set(DYNBITSET_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_pinam45_dynamic_bitset/")
set(OPTIONAL_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_tartanllama_optional/")
set(THREADPOOL_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_progschj_threadpool/")
set(EZPARSER_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_dreal-deps_ezoptionparser/")
set(LIBCDS_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_khizmax_libcds/")
set(JSON_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_nlohmann_json/")
set(PICOSAT_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_dreal-deps_picosat/")

file(GLOB_RECURSE DREAL_SOURCES
        "${PROJECT_SOURCE_DIR}/src/dreal/**.cc"
        "${PROJECT_SOURCE_DIR}/src/dreal/**.yy"
        "${PROJECT_SOURCE_DIR}/src/dreal/**.ll"
        "${DRAKE_HOME}/**.cc"
        "${THREADPOOL_HOME}/**.cc"
        "${LIBCDS_HOME}/src/**.cpp"
        "${PICOSAT_HOME}/src/**.c"
)
add_executable(dreal4_cmake
        ${DREAL_SOURCES}
        ${BISON_smt2_parser_OUTPUTS} ${FLEX_smt2_scanner_OUTPUTS}
        ${BISON_dr_parser_OUTPUTS} ${FLEX_dr_scanner_OUTPUTS}
)
target_include_directories(dreal4_cmake PUBLIC
        "${PARSERS_HOME}"
        "${PROJECT_SOURCE_DIR}/src/"
        "${DRAKE_HOME}"
        "${DYNBITSET_HOME}"
        "${OPTIONAL_HOME}"
        "${THREADPOOL_HOME}"
        "${EZPARSER_HOME}"
        "${LIBCDS_HOME}"
        "${JSON_HOME}"
        "${PICOSAT_HOME}/src/"
)


# system dependencies
target_include_directories(dreal4_cmake PRIVATE "${FLEX_INCLUDE_DIRS}")
target_link_libraries(dreal4_cmake PRIVATE "${FLEX_LIBRARIES}")

find_path(GMP_INC gmpxx.h REQUIRED)
find_library(GMP_C_LIB   libgmp.a   REQUIRED)
find_library(GMP_CXX_LIB libgmpxx.a REQUIRED)
target_include_directories(dreal4_cmake PRIVATE "${GMP_INC}")
target_link_libraries(dreal4_cmake PRIVATE "${GMP_C_LIB}" "${GMP_CXX_LIB}")

find_path(CADICAL_INC cadical.hpp REQUIRED)
find_library(CADICAL_LIB libcadical.a REQUIRED)
target_include_directories(dreal4_cmake PRIVATE "${CADICAL_INC}")
target_link_libraries(dreal4_cmake PRIVATE "${CADICAL_LIB}")


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
        GIT_TAG e559cf412247913776b281acaea3978194372f21)  # Kunal's M1 + namespace fixes
FetchContent_MakeAvailable(ibex)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog/
        GIT_TAG tags/v1.15.0)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
        nlopt
        GIT_REPOSITORY https://github.com/stevengj/nlopt
        GIT_TAG tags/v2.9.1)
FetchContent_MakeAvailable(nlopt)

#FetchContent_Declare(
#        pybind11
#        GIT_REPOSITORY https://github.com/pybind/pybind11
#        GIT_TAG        tags/v2.13.6)
#FetchContent_MakeAvailable(pybind11)
#if(NOT pybind11_POPULATED)
#    # https://stackoverflow.com/questions/47027741/smart-way-to-cmake-a-project-using-pybind11-by-externalproject-add
#    add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR})
#endif()

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # For Windows: Prevent overriding the parent project's compiler/linker settings
FetchContent_MakeAvailable(googletest)

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
        dreal4_cmake PRIVATE ibex nlopt spdlog::spdlog
)
