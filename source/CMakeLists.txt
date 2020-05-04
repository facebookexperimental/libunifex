# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

add_library(unifex "")

target_sources(unifex
  PRIVATE
    async_mutex.cpp
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
      linux/io_epoll_context.cpp)

  target_link_libraries(unifex
    PUBLIC
      pthread)

if (NOT UNIFEX_NO_LIBURING)

  target_sources(unifex
    PRIVATE
      linux/io_uring_context.cpp
      linux/io_uring_syscall.cpp)

  target_include_directories(unifex
    PUBLIC
      ${UNIFEX_URING_INCLUDE_DIRS})
  
  target_link_libraries(unifex
    PRIVATE
      ${UNIFEX_URING_LIBRARY})

endif()

endif()

configure_file(
  ../include/unifex/config.hpp.in
  "${PROJECT_BINARY_DIR}/include/unifex/config.hpp")

target_include_directories(unifex
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include/>
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
)
install(FILES
  "${PROJECT_BINARY_DIR}/include/unifex/config.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/unifex"
)

target_compile_features(unifex PUBLIC cxx_std_${latest_cxx_std})

if(CXX_COROUTINES_HAVE_COROUTINES)
  target_link_libraries(unifex PUBLIC std::coroutines)
endif()

if(COMMAND target_precompile_headers AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../include/unifex.hpp")
  target_precompile_headers(unifex PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include/unifex.hpp>")
endif()

install(DIRECTORY
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/unifex/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/unifex"
)
install(TARGETS unifex
  EXPORT libunifexExports
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
if(BUILD_SHARED_LIBS)
  set_target_properties(unifex PROPERTIES EXPORT_NAME shared)
else()
  set_target_properties(unifex PROPERTIES EXPORT_NAME static)
endif()
