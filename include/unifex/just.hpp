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

template <typename... Values>
class just_sender {
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;

 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<Values...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  template <typename... Values2>
  explicit just_sender(Values2&&... values) noexcept(
      noexcept((std::is_nothrow_constructible_v<Values, Values2> && ...)))
      : values_((Values2 &&) values...) {}

 private:
  template <typename Receiver>
  struct operation {
    UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void start() noexcept {
      try {
        std::apply(
            [&](Values&&... values) {
              cpo::set_value((Receiver &&) receiver_, (Values &&) values...);
            },
            std::move(values_));
      } catch (...) {
        cpo::set_error((Receiver &&) receiver_, std::current_exception());
      }
    }
  };

 public:
  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) && {
    return {std::move(values_), (Receiver &&) r};
  }

  friend constexpr blocking_kind tag_invoke(tag_t<cpo::blocking>, const just_sender&) noexcept {
    return blocking_kind::always_inline;
  }
};

template <typename... Values>
just_sender<std::decay_t<Values>...> just(Values&&... values) noexcept(
    (std::is_nothrow_constructible_v<std::decay_t<Values>, Values> && ...)) {
  return just_sender<std::decay_t<Values>...>{(Values &&) values...};
}

} // namespace unifex
