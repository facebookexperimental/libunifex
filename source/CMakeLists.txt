# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

add_library(unifex "")

target_sources(unifex
  PRIVATE
    inplace_stop_token.cpp
    manual_event_loop.cpp
    trampoline_scheduler.cpp
    thread_unsafe_event_loop.cpp
    timed_single_thread_context.cpp)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(unifex
    PRIVATE
      linux/mmap_region.cpp
      linux/monotonic_clock.cpp
      linux/safe_file_descriptor.cpp
      linux/io_uring_context.cpp)

  target_link_libraries(unifex
    PRIVATE
      pthread
      uring)
endif()

configure_file(
  ../include/unifex/config.hpp.in
  unifex/config.hpp)

target_include_directories(unifex
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include/>
    "${PROJECT_BINARY_DIR}")

target_compile_features(unifex INTERFACE cxx_std_20)

if(UNIFEX_CXX_COMPILER_CLANG)
  target_compile_options(unifex INTERFACE -stdlib=libc++)
endif()
if(CXX_COROUTINES_HAVE_COROUTINES)
  target_link_libraries(unifex INTERFACE std::coroutines)
endif()
