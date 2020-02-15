/*
 * Copyright 2019-present Facebook, Inc.
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

#include <type_traits>

namespace unifex {

template <typename T, typename = void>
struct is_stop_never_possible : std::false_type {};

template <typename T>
struct is_stop_never_possible<
    T,
    std::enable_if_t<std::is_same_v<
        std::false_type,
        std::bool_constant<T{}.stop_possible()>>>> : std::true_type {};

template <typename T>
inline constexpr bool is_stop_never_possible_v = is_stop_never_possible<T>::value;

} // namespace unifex
