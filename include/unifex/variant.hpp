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

#if defined(__cpp_lib_variant) && __cpp_lib_variant > 0
#define UNIFEX_CXX_VARIANT 1
#else
#define UNIFEX_CXX_VARIANT 0
#endif

#if UNIFEX_CXX_VARIANT
#include <variant>
#else
#include <unifex/detail/mpark/variant.hpp>
#endif

#include <unifex/detail/prologue.hpp>

namespace unifex {
#if UNIFEX_CXX_VARIANT
using std::variant;
using std::variant_size;
using std::variant_size_v;
using std::variant_alternative;
using std::variant_npos;
using std::holds_alternative;
using std::visit;
using std::monostate;
using std::bad_variant_access;
namespace var {
using std::get;
using std::get_if;
using std::in_place_t;
using std::in_place_index_t;
using std::in_place_type_t;
using std::in_place;
using std::in_place_index;
using std::in_place_type;
} // namespace var
#else
using mpark::variant;
using mpark::variant_size;
using mpark::variant_size_v;
using mpark::variant_alternative;
using mpark::variant_npos;
using mpark::holds_alternative;
using mpark::visit;
using mpark::monostate;
using mpark::bad_variant_access;
namespace var {
using mpark::get;
using mpark::get_if;
using mpark::in_place_t;
using mpark::in_place_index_t;
using mpark::in_place_type_t;
using mpark::in_place;
using mpark::in_place_index;
using mpark::in_place_type;
} // namespace var
#endif

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
