# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(DefaultBuildType)

project(libunifex LANGUAGES CXX
                  VERSION 0.1)

include(unifex_options)
include(unifex_env)
include(unifex_flags)

add_subdirectory(source)

if(BUILD_TESTING OR UNIFEX_BUILD_EXAMPLES)
  enable_testing()

  include(gtest)
  include(CTest)

  target_compile_features(gtest PUBLIC cxx_std_17)
  target_compile_features(gtest_main PUBLIC cxx_std_17)

  add_subdirectory(examples)
  if(BUILD_TESTING)
    add_subdirectory(test)
  endif(BUILD_TESTING)
endif(BUILD_TESTING OR UNIFEX_BUILD_EXAMPLES)
