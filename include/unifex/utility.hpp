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
#  include <absl/utility/utility.h>
#else
#  include <utility>
#endif

#include <unifex/detail/prologue.hpp>

namespace unifex {
#if defined(UNIFEX_USE_ABSEIL)
using absl::in_place;
using absl::in_place_index_t;
using absl::in_place_t;
using absl::in_place_type_t;
// using absl::in_place_index;
// using absl::in_place_type;
#else
using std::in_place;
using std::in_place_index_t;
using std::in_place_t;
using std::in_place_type_t;
// using std::in_place_index;
// using std::in_place_type;
#endif

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
