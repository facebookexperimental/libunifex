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
#include <unifex/receiver_concepts.hpp>
#include <unifex/blocking.hpp>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace unifex {
namespace _just {

template <typename Receiver, typename... Values>
struct _op {
  struct type;
};
template <typename Receiver, typename... Values>
using operation = typename _op<std::remove_cvref_t<Receiver>, Values...>::type;

template <typename Receiver, typename... Values>
struct _op<Receiver, Values...>::type {
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void start() & noexcept {
    try {
      std::apply(
          [&](Values&&... values) {
            unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
          },
          std::move(values_));
    } catch (...) {
      unifex::set_error((Receiver &&) receiver_, std::current_exception());
    }
  }
};

template <typename... Values>
struct _sender {
  class type;
};
template <typename... Values>
using sender = typename _sender<std::decay_t<Values>...>::type;

template <typename... Values>
class _sender<Values...>::type {
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;

  public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<Values...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  type(const type&) = default;
  type(type&&) = default;

  template <
    typename... Values2,
    std::enable_if_t<
      (sizeof...(Values2) == sizeof...(Values)), int> = 0>
    explicit type(Values2&&... values)
    noexcept(std::is_nothrow_constructible_v<std::tuple<Values...>, Values2...>)
    : values_((Values2 &&) values...) {}

  template <typename Receiver,
    std::enable_if_t<std::is_move_constructible_v<std::tuple<Values...>>, int> = 0>
  auto connect(Receiver&& r) &&
    noexcept(std::is_nothrow_move_constructible_v<std::tuple<Values...>>)
    -> operation<Receiver, Values...> {
    return {std::move(values_), (Receiver &&) r};
  }

  template <typename Receiver,
    std::enable_if_t<std::is_constructible_v<std::tuple<Values...>, std::tuple<Values...>&>, int> = 0>
  auto connect(Receiver&& r) &
    noexcept(std::is_nothrow_constructible_v<std::tuple<Values...>, std::tuple<Values...>&>)
    -> operation<Receiver, Values...> {
    return {values_, (Receiver &&) r};
  }

  template <typename Receiver,
    std::enable_if_t<std::is_constructible_v<std::tuple<Values...>, const std::tuple<Values...>&>, int> = 0>
  auto connect(Receiver&& r) const &
    noexcept(std::is_nothrow_copy_constructible_v<std::tuple<Values...>>)
    -> operation<Receiver, Values...> {
    return {values_, (Receiver &&) r};
  }

  friend constexpr blocking_kind tag_invoke(tag_t<blocking>, const type&) noexcept {
    return blocking_kind::always_inline;
  }
};
} // namespace _just

namespace _just_cpo {
  inline constexpr struct just_fn {
    template<typename... Values>
    constexpr auto operator()(Values&&... values) const
      noexcept(std::is_nothrow_constructible_v<_just::sender<Values...>, Values...>)
      -> _just::sender<std::decay_t<Values>...> {
      return _just::sender<Values...>{(Values&&)values...};
    }
  } just{};
} // namespace _just_cpo
using _just_cpo::just;

} // namespace unifex
