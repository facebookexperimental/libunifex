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

#include <unifex/coroutine.hpp>
#include <unifex/std_concepts.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use <unifex/coroutine_concepts.hpp>"
#endif

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace detail {
template <typename Awaitable, typename = void>
constexpr bool has_member_operator_co_await_v = false;

template <typename Awaitable>
constexpr bool has_member_operator_co_await_v<
    Awaitable,
    std::void_t<decltype(std::declval<Awaitable>().operator co_await())>> =
    true;

template <typename Awaitable, typename = void>
constexpr bool has_free_operator_co_await_v = false;

template <typename Awaitable>
constexpr bool has_free_operator_co_await_v<
    Awaitable,
    std::void_t<decltype(operator co_await(std::declval<Awaitable>()))>> = true;

template <typename Awaiter, typename = void>
struct await_result_impl {};

template <typename Awaiter>
struct await_result_impl<
    Awaiter,
    std::void_t<
        decltype(std::declval<Awaiter&>().await_ready() ? (void)0 : (void)0),
        decltype(std::declval<Awaiter&>().await_resume())>> {
  using type = decltype(std::declval<Awaiter&>().await_resume());
};

} // namespace detail

namespace _get_awaiter {
struct _fn {
  template <typename Awaitable>
  constexpr decltype(auto) operator()(Awaitable&& awaitable) const noexcept {
    if constexpr (detail::has_member_operator_co_await_v<Awaitable>) {
      return static_cast<Awaitable&&>(awaitable).operator co_await();
    } else if constexpr (detail::has_free_operator_co_await_v<Awaitable>) {
      return operator co_await(static_cast<Awaitable&&>(awaitable));
    } else {
      return static_cast<Awaitable&&>(awaitable);
    }
  }
};
} // namespace _get_awaiter

inline constexpr _get_awaiter::_fn get_awaiter {};

template <typename Awaitable>
using awaiter_type_t = decltype(get_awaiter(std::declval<Awaitable>()));

template <typename Awaitable>
using await_result_t =
    typename detail::await_result_impl<awaiter_type_t<Awaitable>>::type;

namespace detail {
  template <typename Awaitable>
  UNIFEX_CONCEPT_FRAGMENT( //
    _awaitable_impl,         //
      requires() (         //
        typename(await_result_t<Awaitable>)
      ));
  template <typename Awaitable>
  UNIFEX_CONCEPT //
    _awaitable = //
      UNIFEX_FRAGMENT(detail::_awaitable_impl, Awaitable);
} // namespace detail

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
