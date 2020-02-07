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

# Probe for libUring support
find_package(LibUring COMPONENTS)
# Set some variables to be used by configure_file.
if(LIBURING_FOUND)
  set(UNIFEX_NO_LIBURING FALSE)
  set(UNIFEX_URING_INCLUDE_DIRS ${LIBURING_INCLUDE_DIRS})
  set(UNIFEX_URING_LIBRARY ${LIBURING_LIBRARIES})
else()
  set(UNIFEX_NO_LIBURING TRUE)
endif()

# Probe for EPOLL support
CHECK_SYMBOL_EXISTS(epoll_create "sys/epoll.h" UNIFEX_HAVE_SYS_EPOLL_CREATE)
if(UNIFEX_HAVE_SYS_EPOLL_CREATE)
  set(UNIFEX_NO_EPOLL FALSE)
else()
  set(UNIFEX_NO_EPOLL TRUE)
endif()
