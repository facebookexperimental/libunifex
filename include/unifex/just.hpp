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

namespace detail {

template <typename Receiver, typename... Values>
struct just_operation {
  struct type {
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
};

template <typename... Values>
struct just_sender {
  class type {
    UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;

   public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<Values...>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    template <typename... Values2>
    explicit type(Values2&&... values)
      noexcept(std::is_nothrow_constructible_v<std::tuple<Values...>, Values2...>)
      : values_((Values2 &&) values...) {}

    template <typename Receiver>
    auto connect(Receiver&& r) &&
      noexcept(std::is_nothrow_move_constructible_v<std::tuple<Values...>>)
      -> typename just_operation<std::remove_cvref_t<Receiver>, Values...>::type {
      return {std::move(values_), (Receiver &&) r};
    }

    template <typename Receiver>
    auto connect(Receiver&& r) &
      noexcept(std::is_nothrow_constructible_v<std::tuple<Values...>, std::tuple<Values...>&>)
      -> typename just_operation<std::remove_cvref_t<Receiver>, Values...>::type {
      return {values_, (Receiver &&) r};
    }

    template <typename Receiver>
    auto connect(Receiver&& r) const &
      noexcept(std::is_nothrow_copy_constructible_v<std::tuple<Values...>>)
      -> typename just_operation<std::remove_cvref_t<Receiver>, Values...>::type {
      return {values_, (Receiver &&) r};
    }

    friend constexpr blocking_kind tag_invoke(tag_t<blocking>, const type&) noexcept {
      return blocking_kind::always_inline;
    }
  };
};

} // namespace detail

inline constexpr struct just_fn {
  template<typename... Values>
  auto operator()(Values&&... values) const
    noexcept(std::conjunction_v<std::is_nothrow_constructible<std::decay_t<Values>, Values>...>)
    -> typename detail::just_sender<std::decay_t<Values>...>::type {
    return typename detail::just_sender<std::decay_t<Values>...>::type{(Values&&)values...};
  }
} just;

} // namespace unifex
