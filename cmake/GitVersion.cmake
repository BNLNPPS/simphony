# GitVersion.cmake
#
# Derives PROJECT_VERSION_STRING and PROJECT_GIT_REVISION from git when available.
# Falls back to PROJECT_VERSION when git is not present (e.g. Docker builds).
#
# Sets in the caller's scope:
#   PROJECT_VERSION_STRING - Version string: exact tag ("0.3.0") or
#                            tag+distance+sha ("0.3.0-2-gabcdef-dirty").
#                            Falls back to PROJECT_VERSION.
#   PROJECT_GIT_REVISION   - Short commit hash, or empty string if git is unavailable.

find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "[0-9]*" --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_rev_result
    )
    if(_git_rev_result)
        set(PROJECT_GIT_REVISION "")
    endif()
else()
    set(_git_result 1)
    set(PROJECT_GIT_REVISION "")
endif()

if(_git_result EQUAL 0 AND _git_describe)
    set(PROJECT_VERSION_STRING "${_git_describe}")
    message(STATUS "Version from git: ${PROJECT_VERSION_STRING}")
else()
    set(PROJECT_VERSION_STRING "${PROJECT_VERSION}")
    message(STATUS "Version from project(): ${PROJECT_VERSION_STRING} (git not available)")
endif()
