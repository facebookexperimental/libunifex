# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)

project(libunifex LANGUAGES CXX
                  VERSION 0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
if(NOT DEFINED PROJECT_IS_DEPENDENCY)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PROJECT_IS_DEPENDENCY OFF)
  else()
    set(PROJECT_IS_DEPENDENCY ON)
  endif()
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_GENERATOR_INSTANCE)
  include(DefaultBuildType)
endif()

include(unifex_options)
include(unifex_env)
include(unifex_flags)

add_subdirectory(source)

if(BUILD_TESTING OR UNIFEX_BUILD_EXAMPLES)
  # We enable testing in both cases
  enable_testing()
  include(CTest)
  # Examples do not use gtest
  if(UNIFEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
  endif(UNIFEX_BUILD_EXAMPLES)
  # Tests do use gtest
  if(BUILD_TESTING)
    include(gtest)
    target_compile_features(gtest PUBLIC cxx_std_17)
    target_compile_features(gtest_main PUBLIC cxx_std_17)
    add_subdirectory(test)
  endif(BUILD_TESTING)
endif(BUILD_TESTING OR UNIFEX_BUILD_EXAMPLES)
