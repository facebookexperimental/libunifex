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

#include <unifex/config.hpp>

#include <type_traits>

namespace unifex {

template <typename T>
struct identity {
  using type = T;
};

template<typename... Ts>
struct single_type {};

template<typename T>
struct single_type<T> {
    using type = T;
};

template<typename... Ts>
using single_type_t = typename single_type<Ts...>::type;

template <template <typename T> class Predicate, typename T>
using requires_t = std::enable_if_t<Predicate<T>::value, T>;

template <template<typename...> class T, typename X>
struct instance_of : std::false_type {};

template <template<typename...> class T, typename... Args>
struct instance_of<T, T<Args...>> : std::true_type {};

template <template<typename...> class T, typename X>
constexpr bool instance_of_v = instance_of<T, X>::value;

struct unit {};

template <typename T>
using non_void_t = std::conditional_t<std::is_void_v<T>, unit, T>;

template <typename T>
using wrap_reference_t = std::conditional_t<
    std::is_reference_v<T>,
    std::reference_wrapper<std::remove_reference_t<T>>,
    T>;

template <typename T>
using decay_rvalue_t = std::
    conditional_t<std::is_lvalue_reference_v<T>, T, std::remove_cvref_t<T>>;

template <typename... Args>
struct is_empty_list : std::bool_constant<(sizeof...(Args) == 0)> {};

template <typename T>
struct is_nothrow_constructible_from {
  template <typename... Args>
  using apply = std::is_nothrow_constructible<T, Args...>;
};

template <template <typename...> class Tuple>
struct decayed_tuple {
  template <typename... Ts>
  using apply = Tuple<std::remove_cvref_t<Ts>...>;
};

template <typename T, typename... Ts>
constexpr bool is_one_of_v = (std::is_same_v<T, Ts> || ...);

} // namespace unifex
