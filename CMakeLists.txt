# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.6)

project(libunifex CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(unifex_options)
include(unifex_env)
include(unifex_flags)

add_library(unifex-interface INTERFACE)

target_include_directories(unifex-interface
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>)
target_compile_features(unifex-interface INTERFACE cxx_std_20)
if(UNIFEX_CXX_COMPILER_CLANG)
  target_compile_options(unifex-interface INTERFACE -stdlib=libc++)
endif()
if(UNIFEX_COROUTINE_FLAGS)
  target_compile_options(unifex-interface INTERFACE ${UNIFEX_COROUTINE_FLAGS})
else()
  target_compile_definitions(unifex-interface INTERFACE "UNIFEX_NO_COROUTINES=1")
endif()

enable_testing()

add_subdirectory(source)
add_subdirectory(examples)
