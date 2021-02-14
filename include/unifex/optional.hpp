/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/config.hpp>

#if defined(__cpp_lib_optional) && __cpp_lib_optional > 0
#define UNIFEX_CXX_OPTIONAL 1
#define UNIFEX_CXX_EXPERIMENTAL_OPTIONAL 0
#elif defined(__cpp_lib_experimental_optional) && __cpp_lib_experimental_optional > 0
#define UNIFEX_CXX_OPTIONAL 0
#define UNIFEX_CXX_EXPERIMENTAL_OPTIONAL 1
#else
#error No <optional> header found
#endif

#if UNIFEX_CXX_OPTIONAL
#include <optional>
#elif UNIFEX_CXX_EXPERIMENTAL_OPTIONAL
#include <experimental/optional>
#endif

#include <unifex/detail/prologue.hpp>

namespace unifex {
#if UNIFEX_CXX_OPTIONAL
using std::optional;
using std::nullopt;
using std::nullopt_t;
using std::make_optional;
using std::bad_optional_access;
#elif UNIFEX_CXX_EXPERIMENTAL_OPTIONAL
using std::experimental::optional;
using std::experimental::nullopt;
using std::experimental::nullopt_t;
using std::experimental::make_optional;
using std::experimental::bad_optional_access;
#endif
}

#include <unifex/detail/epilogue.hpp>
