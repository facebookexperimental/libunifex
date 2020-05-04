# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)

if (${CMAKE_BINARY_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
  message(FATAL_ERROR "FATAL: Source and Binary directory cannot be the same")
endif()

include("cmake/HunterVersion.cmake")

project(libunifex LANGUAGES CXX C
                  VERSION 0.1)
include(GNUInstallDirs)
if(NOT DEFINED PROJECT_IS_DEPENDENCY)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PROJECT_IS_DEPENDENCY OFF)
  else()
    set(PROJECT_IS_DEPENDENCY ON)
  endif()
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_GENERATOR_INSTANCE)
  message(FATAL_ERROR "FATAL: CMAKE_BUILD_TYPE must be set on non-multi-config generators")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(unifex_options)
include(unifex_env)
include(unifex_flags)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/llfio/include/llfio/ntkernel-error-category/include")
  include(FindGit)
  message(STATUS "NOTE: Running ${GIT_EXECUTABLE} submodule update --init --recursive on external projects ...")
  execute_process(COMMAND "${GIT_EXECUTABLE}" submodule update --init --recursive
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/llfio"
    RESULT_VARIABLE retcode
  )
  if(retcode)
    message(FATAL_ERROR "FATAL: git submodule update failed with code ${retcode}")
  endif()
endif()
if(FALSE)  ## We shall use hunter later, but not for now
  hunter_add_package(llfio)
  find_package(llfio CONFIG REQUIRED)
else()
  set(former_dependency ${PROJECT_IS_DEPENDENCY})
  if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(former_cxx_standard undefined)
  else()
    set(former_cxx_standard ${CMAKE_CXX_STANDARD})
  endif()
  set(PROJECT_IS_DEPENDENCY On)
  set(CMAKE_CXX_STANDARD ${latest_cxx_std})
  add_subdirectory("external/llfio" EXCLUDE_FROM_ALL)
  set(PROJECT_IS_DEPENDENCY ${former_dependency})
  if(former_cxx_standard STREQUAL undefined)
    set(CMAKE_CXX_STANDARD)
  else()
    set(CMAKE_CXX_STANDARD ${former_cxx_standard})
  endif()
endif()
if(NOT TARGET outcome::hl)
  find_package(outcome CONFIG REQUIRED HINTS "${CMAKE_BINARY_DIR}/outcome")
  find_package(quickcpplib CONFIG REQUIRED HINTS "${CMAKE_BINARY_DIR}/quickcpplib")
endif()

add_subdirectory(source)

if(NOT PROJECT_IS_DEPENDENCY AND (BUILD_TESTING OR UNIFEX_BUILD_EXAMPLES))
  enable_testing()

  hunter_add_package(GTest)
  find_package(GTest CONFIG REQUIRED)
  include(CTest)

  add_subdirectory(examples)
  if(BUILD_TESTING)
    add_subdirectory(test)
  endif(BUILD_TESTING)
endif(NOT PROJECT_IS_DEPENDENCY AND (BUILD_TESTING OR UNIFEX_BUILD_EXAMPLES))

# Installation is disabled until decision is taken on how best to do liburing
# (see https://github.com/facebookexperimental/libunifex/issues/103)
#configure_file(
#  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectConfig.cmake.in"
#  "${CMAKE_CURRENT_BINARY_DIR}/unifexConfig.cmake"
#  @ONLY
#)
#install(FILES
#  "${CMAKE_CURRENT_BINARY_DIR}/unifexConfig.cmake"
#  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/unifex"
#)
#install(EXPORT libunifexExports
#  NAMESPACE unifex::
#  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/unifex"
#)
