# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the license found in the
# LICENSE.txt file in the root directory of this source tree.

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
include(CheckIncludeFile)
include(CheckSymbolExists)

# Probe for coroutine TS support
find_package(Coroutines COMPONENTS Experimental Final)
# MSVC's coroutines support is too broken to support here, sadly.
if(CXX_COROUTINES_HAVE_COROUTINES AND NOT UNIFEX_CXX_COMPILER_MSVC)
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

if(DEFINED UNIFEX_NO_LIBURING)
  message(WARNING "[unifex warning]: forcing no_liburing=${UNIFEX_NO_LIBURING} !")
elseif(DEFINED ENV{UNIFEX_NO_LIBURING})
  message(WARNING "[unifex warning]: forcing no_liburing=$ENV{UNIFEX_NO_LIBURING} !")
  set(UNIFEX_NO_LIBURING $ENV{UNIFEX_NO_LIBURING})
  set(LIBURING_INCLUDE_DIRS $ENV{UNIFEX_LIBURING_INCLUDE_DIRS})
  set(LIBURING_LIBRARIES $ENV{UNIFEX_LIBURING_LIBRARIES})
else()
# Probe for libUring support
find_package(LibUring COMPONENTS)
# Set some variables to be used by configure_file.
if(LIBURING_FOUND)
  set(UNIFEX_NO_LIBURING FALSE)
else()
  set(UNIFEX_NO_LIBURING TRUE)
endif()
endif()

if(NOT UNIFEX_NO_LIBURING)
  set(UNIFEX_URING_INCLUDE_DIRS ${LIBURING_INCLUDE_DIRS})
  set(UNIFEX_URING_LIBRARY ${LIBURING_LIBRARIES})
endif()

if(DEFINED UNIFEX_NO_EPOLL)
  message(WARNING "[unifex warning]: forcing no_epoll=${UNIFEX_NO_EPOLL} !")
elseif(DEFINED ENV{UNIFEX_NO_EPOLL})
  message(WARNING "[unifex warning]: forcing no_epoll=$ENV{UNIFEX_NO_EPOLL} !")
  set(UNIFEX_NO_EPOLL $ENV{UNIFEX_NO_EPOLL})
else()
# Probe for EPOLL support
CHECK_SYMBOL_EXISTS(epoll_create "sys/epoll.h" UNIFEX_HAVE_SYS_EPOLL_CREATE)
if(UNIFEX_HAVE_SYS_EPOLL_CREATE)
  set(UNIFEX_NO_EPOLL FALSE)
else()
  set(UNIFEX_NO_EPOLL TRUE)
endif()
endif()
