# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

include(CheckCXXCompilerFlag)

# Require a cmake and compiler with at least C++ 20
set(latest_cxx_std)
if(NOT latest_cxx_std)
  foreach(feature ${CMAKE_CXX_COMPILE_FEATURES})
    if(feature STREQUAL "cxx_std_23")
      set(latest_cxx_std "23")
    endif()
  endforeach()
endif()
if(NOT latest_cxx_std)
  foreach(feature ${CMAKE_CXX_COMPILE_FEATURES})
    if(feature STREQUAL "cxx_std_20")
      set(latest_cxx_std "20")
    endif()
  endforeach()
endif()
if(NOT latest_cxx_std OR latest_cxx_std LESS 20)
  message(FATAL_ERROR "FATAL: To build this project requires a compiler and cmake supporting C++ 20 or later, this compiler and cmake claims to only support C++ ${latest_cxx_std}")
endif()
message(STATUS "Source code shall be built using C++ ${latest_cxx_std} which is the latest this compiler claims to support")

if("x${CMAKE_CXX_COMPILER_ID}" MATCHES "x.*Clang")
  if("x${CMAKE_CXX_SIMULATE_ID}" STREQUAL "xMSVC")
    set (UNIFEX_CXX_COMPILER_CLANGCL TRUE)
  else()
    set (UNIFEX_CXX_COMPILER_CLANG TRUE)
  endif()
elseif(CMAKE_COMPILER_IS_GNUCXX)
  set (UNIFEX_CXX_COMPILER_GCC TRUE)
elseif("x${CMAKE_CXX_COMPILER_ID}" STREQUAL "xMSVC")
  set (UNIFEX_CXX_COMPILER_MSVC TRUE)
else()
  message(WARNING "[unifex warning]: unknown compiler ${CMAKE_CXX_COMPILER_ID} !")
endif()

if(WIN32)
  add_compile_definitions(NOMINMAX)
  add_compile_definitions(WIN32_LEAN_AND_MEAN)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS=1)
  add_compile_options(/GS)                    ## Check for buffer overflows
  add_compile_options(/Gw)                    ## Place global data in separate COMDATs
  add_compile_options(/Gy)                    ## Place functions in separate COMDATs
  add_compile_options(/bigobj)                ## Use new object file format
  add_compile_options(/Zc:__cplusplus)        ## Actually say we on a newer C++ standard
  add_compile_options(/EHsc)                  ## Enable exceptions except for C functions
  add_compile_options(/permissive-)           ## Be more compliant with the C++ standard

  if(CMAKE_GENERATOR MATCHES "Ninja")
    if(CMAKE_BUILD_TYPE MATCHES "Debug")
      add_compile_options(/MDd)
    else()
      add_compile_options(/MD)
    endif()
  else()
    add_compile_options(/MP)                  ## Multiprocess build
  endif()
else()
  add_compile_options(-Wall -Wextra)          ## Stronger warnings
endif()
