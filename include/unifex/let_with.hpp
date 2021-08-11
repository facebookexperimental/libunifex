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
#pragma once

#include <unifex/config.hpp>
#include <unifex/let_value_with.hpp>

#include <unifex/detail/prologue.hpp>

UNIFEX_DEPRECATED_HEADER("let_with.hpp is deprecated. Use let_value_with.hpp instead.")

namespace unifex {
[[deprecated("unifex::let_with has been renamed to unifex::let_value_with")]]
inline constexpr _let_v_w::_cpo::_fn let_with {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
