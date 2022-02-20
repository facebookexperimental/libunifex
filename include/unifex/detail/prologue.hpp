/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// No include guard or pragma once. This file is intended to be included
// multiple times.

#ifdef UNIFEX_PROLOGUE_HPP
#error Prologue has already been included
#endif
#define UNIFEX_PROLOGUE_HPP

#include <unifex/config.hpp>

UNIFEX_DIAGNOSTIC_PUSH

#if defined(__clang__)
    #pragma clang diagnostic ignored "-Wkeyword-macro"
#endif

#if UNIFEX_CXX_CONCEPTS
  #define template(...) \
    template <__VA_ARGS__> UNIFEX_PP_EXPAND \
    /**/
#else
  #define template(...) \
    template <__VA_ARGS__ UNIFEX_TEMPLATE_SFINAE_AUX_ \
    /**/
#endif

UNIFEX_DIAGNOSTIC_POP

#define AND UNIFEX_AND
