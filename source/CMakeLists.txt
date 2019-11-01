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

target_include_directories(unifex
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include/>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR})

# Link against libstdc++ implementation of std::filesystem
target_link_libraries(unifex
  PRIVATE
    stdc++fs)
