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
#  include <absl/types/optional.h>
#else
#  include <optional>
#endif

#include <unifex/utility.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
#if defined(UNIFEX_USE_ABSEIL)
using absl::bad_optional_access;
using absl::make_optional;
using absl::nullopt;
using absl::nullopt_t;
using absl::optional;
#else
using std::bad_optional_access;
using std::make_optional;
using std::nullopt;
using std::nullopt_t;
using std::optional;
#endif
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
