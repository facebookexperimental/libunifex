# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
include(CheckIncludeFile)
include(CheckSymbolExists)

# Probe for coroutine TS support
find_package(Coroutines COMPONENTS Experimental Final)
if(CXX_COROUTINES_HAVE_COROUTINES)
  set(UNIFEX_NO_COROUTINES FALSE)
  set(UNIFEX_COROUTINES_HEADER ${CXX_COROUTINES_HEADER})
  set(UNIFEX_COROUTINES_NAMESPACE ${CXX_COROUTINES_NAMESPACE})
else()
  set(UNIFEX_NO_COROUTINES TRUE)
endif()

# Probe for memory_resource support
find_package(MemoryResource COMPONENTS Experimental Final)
# Set some variables to be used by configure_file.
if(CXX_MEMORY_RESOURCE_HAVE_PMR)
  set(UNIFEX_NO_MEMORY_RESOURCE FALSE)
  set(UNIFEX_MEMORY_RESOURCE_HEADER ${CXX_MEMORY_RESOURCE_HEADER})
  set(UNIFEX_MEMORY_RESOURCE_NAMESPACE ${CXX_MEMORY_RESOURCE_NAMESPACE})
else()
  set(UNIFEX_NO_MEMORY_RESOURCE TRUE)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  # Probe for libUring support
  find_package(LibUring QUIET COMPONENTS)
  # Set some variables to be used by configure_file.
  if(LIBURING_FOUND)
    set(UNIFEX_NO_LIBURING FALSE)
    set(UNIFEX_URING_INCLUDE_DIRS ${LIBURING_INCLUDE_DIRS})
    set(UNIFEX_URING_LIBRARY ${LIBURING_LIBRARIES})
  else()
    message(STATUS "system liburing not found, downloading and superbuilding a local copy ...")
    function(download_build_install)
      function(checked_execute_process desc)
        execute_process(${ARGN}
          RESULT_VARIABLE result
          OUTPUT_VARIABLE out
          ERROR_VARIABLE errout
        )
        if(NOT result EQUAL 0)
          message(FATAL_ERROR "FATAL: ${desc} failed with error '${result}'\n\nstdout was: ${out}\n\nstderr was: ${errout}")
        endif()
        #message("stdout was: ${out}\n\nstderr was: ${errout}")
      endfunction()
      cmake_parse_arguments(DBI "" "NAME;DESTINATION;GIT_REPOSITORY;GIT_TAG" "CMAKE_ARGS;EXTERNALPROJECT_ARGS" ${ARGN})
      configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DownloadBuildInstall.cmake.in" "${DBI_DESTINATION}/CMakeLists.txt" @ONLY)
      checked_execute_process("Configure download, build and install of ${DBI_NAME} with ${DBI_CMAKE_ARGS}"
        COMMAND "${CMAKE_COMMAND}" .
        WORKING_DIRECTORY "${DBI_DESTINATION}"
      )
      checked_execute_process("Execute download, build and install of ${DBI_NAME} with ${DBI_CMAKE_ARGS}" 
        COMMAND "${CMAKE_COMMAND}" --build .
        WORKING_DIRECTORY "${DBI_DESTINATION}"
      )
    endfunction()
    download_build_install(NAME liburing DESTINATION "${CMAKE_BINARY_DIR}/liburing/repo" 
      GIT_REPOSITORY "https://github.com/axboe/liburing"
      GIT_TAG "master"
      EXTERNALPROJECT_ARGS "GIT_SHALLOW TRUE\n  GIT_PROGRESS TRUE\n  CONFIGURE_COMMAND ./configure --prefix=${CMAKE_BINARY_DIR}/liburing/install\n  BUILD_COMMAND make -j 4\n  BUILD_IN_SOURCE TRUE\n  INSTALL_COMMAND make install\n  LOG_CONFIGURE TRUE\n  LOG_BUILD TRUE\n  LOG_INSTALL TRUE"
    )
    find_library(UNIFEX_URING_LIBRARY liburing.a HINTS "${CMAKE_BINARY_DIR}/liburing/install/lib")
    if(UNIFEX_URING_LIBRARY MATCHES "NOTFOUND")
      message(FATAL_ERROR "FATAL: Superbuild of local copy of liburing failed!")
    endif()
    set(UNIFEX_NO_LIBURING FALSE)
    set(UNIFEX_URING_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/liburing/install/include")
  endif()
endif()

# Probe for EPOLL support
CHECK_SYMBOL_EXISTS(epoll_create "sys/epoll.h" UNIFEX_HAVE_SYS_EPOLL_CREATE)
if(UNIFEX_HAVE_SYS_EPOLL_CREATE)
  set(UNIFEX_NO_EPOLL FALSE)
else()
  set(UNIFEX_NO_EPOLL TRUE)
endif()
