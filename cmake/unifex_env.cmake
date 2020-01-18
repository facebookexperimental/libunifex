# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

include(CheckCXXCompilerFlag)

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

if(UNIFEX_CXX_COMPILER_MSVC OR UNIFEX_CXX_COMPILER_CLANGCL)
  set(UNIFEX_CXX_STD "/std:latest")
else()
  set(UNIFEX_CXX_STD "-std=gnu++2a")
endif()

if(UNIFEX_CXX_COMPILER_CLANG)
  add_compile_options(-stdlib=libc++)
  link_libraries(c++)
endif()
