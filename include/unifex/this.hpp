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
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

struct this_ {};

template <typename T>
inline constexpr bool is_this_v = false;
template <>
inline constexpr bool is_this_v<this_> = true;
template <>
inline constexpr bool is_this_v<this_&> = true;
template <>
inline constexpr bool is_this_v<this_&&> = true;
template <>
inline constexpr bool is_this_v<const this_> = true;
template <>
inline constexpr bool is_this_v<const this_&> = true;
template <>
inline constexpr bool is_this_v<const this_&&> = true;

template <typename>
struct _replace_this;

template <>
struct _replace_this<void> {
  template <typename Arg, typename>
  using apply = Arg;

  template <typename Arg>
  static Arg&& get(Arg&& arg, detail::_ignore) noexcept {
    return (Arg &&) arg;
  }
};

template <>
struct _replace_this<this_> {
  template <typename, typename T>
  using apply = T;

  template <typename T>
  static T&& get(detail::_ignore, T& obj) noexcept {
    return (T &&) obj;
  }
};

template <>
struct _replace_this<this_&> {
  template <typename, typename T>
  using apply = T&;

  template <typename T>
  static T& get(detail::_ignore, T& obj) noexcept {
    return obj;
  }
};

template <>
struct _replace_this<this_&&> {
  template <typename, typename T>
  using apply = T&&;

  template <typename T>
  static T&& get(detail::_ignore, T& obj) noexcept {
    return (T &&) obj;
  }
};

template <>
struct _replace_this<const this_&> {
  template <typename, typename T>
  using apply = const T&;

  template <typename T>
  static const T& get(detail::_ignore, T& obj) noexcept {
    return obj;
  }
};

template <>
struct _replace_this<const this_&&> {
  template <typename, typename T>
  using type = const T&&;

  template <typename T>
  static const T&& get(detail::_ignore, T& obj) noexcept {
    return (const T&&) obj;
  }
};

template <typename Arg>
using _normalize_t = conditional_t<is_this_v<Arg>, Arg, void>;

template <typename T>
using replace_this = _replace_this<_normalize_t<T>>;

template <typename Arg, typename T>
using replace_this_t = typename replace_this<Arg>::template apply<Arg, T>;

template <typename Arg>
using replace_this_with_void_ptr_t =
    conditional_t<is_this_v<Arg>, void*, Arg>;

template <bool...>
struct _extract_this {
  template <typename TFirst, typename... TRest>
  TFirst&& operator()(TFirst&& first, TRest&&...) const noexcept {
    return (TFirst&&) first;
  }
};
template <bool... IsThis>
struct _extract_this<false, IsThis...> {
  template <typename... TRest>
  decltype(auto) operator()(detail::_ignore, TRest&&... rest) const noexcept {
    static_assert(sizeof...(IsThis) > 0, "Arguments to extract_this");
    return _extract_this<IsThis...>{}((TRest &&) rest...);
  }
};

template <typename... Ts>
using extract_this = _extract_this<is_this_v<Ts>...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
