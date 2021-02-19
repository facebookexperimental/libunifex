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

#if defined(UNIFEX_USE_ABSEIL)
#include <absl/types/variant.h>
#else
#include <variant>
#endif

#include <unifex/utility.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
#if defined(UNIFEX_USE_ABSEIL)
using absl::variant;
using absl::variant_size;
using absl::variant_alternative;
using absl::variant_npos;
using absl::holds_alternative;
using absl::visit;
using absl::monostate;
using absl::bad_variant_access;
namespace var {
using absl::get;
using absl::get_if;
}
#else
using std::variant;
using std::variant_size;
using std::variant_alternative;
using std::variant_npos;
using std::holds_alternative;
using std::visit;
using std::monostate;
using std::bad_variant_access;
namespace var {
using std::get;
using std::get_if;
}
#endif
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
