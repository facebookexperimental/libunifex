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

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename T, typename = void>
inline constexpr bool is_stop_never_possible_v = false;

template <typename T>
inline constexpr bool is_stop_never_possible_v<
    T,
    std::enable_if_t<std::is_same_v<
        std::false_type,
        std::bool_constant<(T{}.stop_possible())>>>> = true;

template <typename T>
using is_stop_never_possible = std::bool_constant<is_stop_never_possible_v<T>>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
