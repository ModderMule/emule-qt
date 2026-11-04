# Idempotent patch step for FetchContent PATCH_COMMAND (runs in the source dir):
#   ${CMAKE_COMMAND} -DPATCH=<file.patch> -P cmake/ApplyPatch.cmake
# FetchContent reruns PATCH_COMMAND after its update step, so an applied patch is skipped
if(NOT PATCH OR NOT EXISTS "${PATCH}")
    message(FATAL_ERROR "ApplyPatch: PATCH not set or missing: ${PATCH}")
endif()
find_package(Git REQUIRED QUIET)
get_filename_component(_name "${PATCH}" NAME)

execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH}"
    RESULT_VARIABLE _applied OUTPUT_QUIET ERROR_QUIET)
if(_applied EQUAL 0)
    message(STATUS "ApplyPatch: ${_name} already applied")
    return()
endif()

execute_process(COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PATCH}"
    RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "ApplyPatch: ${_name} failed:\n${_err}")
endif()
message(STATUS "ApplyPatch: ${_name} applied")
