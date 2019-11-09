# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

add_library(unifex "")

target_sources(unifex
  PRIVATE
    inplace_stop_token.cpp
    manual_event_loop.cpp
    static_thread_pool.cpp
    thread_unsafe_event_loop.cpp
    timed_single_thread_context.cpp
    trampoline_scheduler.cpp)

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

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Assuming using libc++ with Clang
  target_link_libraries(unifex
    PRIVATE
      -lc++experimental)
endif()

target_include_directories(unifex
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include/>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR})

