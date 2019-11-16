# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.6)

project(libunifex CXX)

# set the project name and version
project(libunifex VERSION 0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(unifex_options)
include(unifex_env)
include(unifex_flags)

configure_file(include/unifex/config.hpp.in unifex/config.hpp)

add_library(unifex-interface INTERFACE)

target_include_directories(unifex-interface
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
    "${PROJECT_BINARY_DIR}") # find the generated config header
target_compile_features(unifex-interface INTERFACE cxx_std_20)
if(UNIFEX_CXX_COMPILER_CLANG)
  target_compile_options(unifex-interface INTERFACE -stdlib=libc++)
endif()
if(CXX_COROUTINES_HAVE_COROUTINES)
  target_link_libraries(unifex-interface INTERFACE std::coroutines)
endif()

enable_testing()

add_subdirectory(source)
add_subdirectory(examples)
