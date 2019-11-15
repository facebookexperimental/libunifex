# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)

# Probe for coroutine TS support
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/coro_test_code.cpp" UNIFEX_PROBE_CODE)
if(UNIFEX_CXX_COMPILER_MSVC)
  set(CMAKE_REQUIRED_FLAGS "/await")
  check_cxx_source_compiles("${UNIFEX_PROBE_CODE}" UNIFEX_HAS_AWAIT)
  if(UNIFEX_HAS_AWAIT)
    set(UNIFEX_COROUTINE_FLAGS "/await")
  endif()
elseif(UNIFEX_CXX_COMPILER_CLANG)
  set(CMAKE_REQUIRED_FLAGS "-fcoroutines-ts")
  check_cxx_source_compiles("${UNIFEX_PROBE_CODE}" UNIFEX_HAS_FCOROUTINES_TS)
  if(UNIFEX_HAS_FCOROUTINES_TS)
    set(UNIFEX_COROUTINE_FLAGS "-fcoroutines-ts")
  endif()
endif()
unset(CMAKE_REQUIRED_FLAGS)
unset(UNIFEX_PROBE_CODE)
if (UNIFEX_COROUTINE_FLAGS)
  add_compile_options(${UNIFEX_COROUTINE_FLAGS})
else()
  add_compile_definitions("UNIFEX_NO_COROUTINES=1")
endif()

# Probe for PMR support
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pmr_test_code.cpp" UNIFEX_PROBE_CODE)
set(CMAKE_REQUIRED_FLAGS ${UNIFEX_CXX_STD})
check_cxx_source_compiles("${UNIFEX_PROBE_CODE}" UNIFEX_HAS_MEMORY_RESOURCE)
unset(CMAKE_REQUIRED_FLAGS)
unset(UNIFEX_PROBE_CODE)

# Probe for experimental PMR support
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pmr_experimental_test_code.cpp" UNIFEX_PROBE_CODE)
set(CMAKE_REQUIRED_FLAGS ${UNIFEX_CXX_STD})
if(UNIFEX_CXX_COMPILER_CLANG)
  set(CMAKE_REQUIRED_LIBRARIES "-lc++experimental")
  if(NOT "x${UNIFEX_STDLIB_PATH}" STREQUAL "x")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-L${UNIFEX_STDLIB_PATH}")
  endif()
endif()
check_cxx_source_compiles("${UNIFEX_PROBE_CODE}" UNIFEX_HAS_EXPERIMENTAL_MEMORY_RESOURCE)
if(UNIFEX_HAS_EXPERIMENTAL_MEMORY_RESOURCE AND NOT UNIFEX_HAS_MEMORY_RESOURCE)
  set(UNIFEX_PMR_LIBRARY "-lc++experimental")
  if(NOT "x${UNIFEX_STDLIB_PATH}" STREQUAL "x")
    set(UNIFEX_PMR_LINK_PATH "-L${UNIFEX_STDLIB_PATH}")
  endif()
endif()
unset(CMAKE_REQUIRED_LINK_OPTIONS)
unset(CMAKE_REQUIRED_LIBRARIES)
unset(CMAKE_REQUIRED_FLAGS)
unset(UNIFEX_PROBE_CODE)

if(NOT UNIFEX_HAS_MEMORY_RESOURCE AND NOT UNIFEX_HAS_EXPERIMENTAL_MEMORY_RESOURCE)
  add_compile_definitions("UNIFEX_NO_MEMORY_RESOURCE=1")
endif()
