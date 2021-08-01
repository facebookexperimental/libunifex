# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the license found in the
# LICENSE.txt file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)

project(libunifex LANGUAGES CXX
                  VERSION 0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(project-is-top-level)
include(unifex_options)
include(unifex_env)
include(unifex_flags)

# The library target is defined in the source subdirectory
add_subdirectory(source)

# Don't build tests or examples if this project is not a top-level project
# and only consumed via add_subdirectory in a git submodule fashion
if (PROJECT_IS_TOP_LEVEL)
  # We enable testing in both cases, either if doing tests or building examples
  # Note, that examples are also registered as tests by subsequent calls to add_test()
  # You can run all tests or examples at once by using `ctest` or `ninja test`
  include(CTest)
  if(BUILD_TESTING OR UNIFEX_BUILD_EXAMPLES)
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
endif(PROJECT_IS_TOP_LEVEL)