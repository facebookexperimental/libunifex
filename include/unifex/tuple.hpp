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

#include <functional>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex {
#if UNIFEX_CXX_APPLY

using std::apply;

#else // UNIFEX_CXX_APPLY

namespace _apply {
template <typename F, typename Tuple, std::size_t... I>
constexpr auto _impl(F&& f, Tuple&& t, std::integer_sequence<std::size_t, I...>)
  noexcept(noexcept(unifex::invoke((F&&) f, std::get<I>((Tuple&&) t)...))) ->
  decltype(unifex::invoke((F&&) f, std::get<I>((Tuple&&) t)...)) {
  return unifex::invoke((F&&) f, std::get<I>((Tuple&&) t)...);
}
template <typename Tuple>
using _indices_for =
  std::make_integer_sequence<
    std::size_t,
    std::tuple_size<std::decay_t<Tuple>>::value>;
} // namespace _apply

template <typename F, typename Tuple>
constexpr auto apply(F&& f, Tuple&& t)
  noexcept(noexcept(
    _apply::_impl((F&&) f, (Tuple&&) t, _apply::_indices_for<Tuple>{}))) ->
  decltype(
    _apply::_impl((F&&) f, (Tuple&&) t, _apply::_indices_for<Tuple>{})) {
  return _apply::_impl((F&&) f, (Tuple&&) t, _apply::_indices_for<Tuple>{});
}

#endif // UNIFEX_CXX_INVOKE
}

#include <unifex/detail/epilogue.hpp>
