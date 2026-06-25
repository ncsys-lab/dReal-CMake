# Invoked at every build by the always-run CMake custom target git_version_h
# (CMakeLists.txt). Writes OUTPUT_FILE (gcc_build/git_version.h) only when
# content changes (copy_if_different) so dreal_main.cc is not recompiled
# unnecessarily.  In Docker the .git/objects/ store is absent, so git status
# fails and dirty is 0; git rev-parse still works because HEAD + refs/ are
# copied into the image by the Dockerfile.
#
# Inputs passed via -D: GIT_EXECUTABLE, OUTPUT_FILE.

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_HASH_RESULT
)
if(NOT GIT_HASH_RESULT EQUAL 0 OR NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_STATUS_RESULT
)
if(GIT_STATUS_RESULT EQUAL 0 AND GIT_STATUS)
    set(GIT_DIRTY 1)
else()
    set(GIT_DIRTY 0)
endif()

set(_content
"#pragma once
#define DREAL_GIT_HASH \"${GIT_HASH}\"
#define DREAL_GIT_DIRTY ${GIT_DIRTY}
")
set(_tmp "${OUTPUT_FILE}.tmp")
file(WRITE "${_tmp}" "${_content}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_tmp}" "${OUTPUT_FILE}")
file(REMOVE "${_tmp}")
