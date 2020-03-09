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

#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <type_traits>

namespace unifex {

inline constexpr struct set_value_cpo {
  template <typename Receiver, typename... Values>
  friend auto
  tag_invoke(set_value_cpo, Receiver&& r, Values&&... values) noexcept(
      noexcept(static_cast<Receiver&&>(r).set_value((Values &&) values...)))
      -> decltype(static_cast<Receiver&&>(r).set_value((Values &&) values...)) {
    return static_cast<Receiver&&>(r).set_value((Values &&) values...);
  }

  template <typename Receiver, typename... Values>
  auto operator()(Receiver&& r, Values&&... values) const noexcept(
      noexcept(tag_invoke(*this, (Receiver &&) r, (Values &&) values...)))
      -> tag_invoke_result_t<set_value_cpo, Receiver, Values...> {
    static_assert(
      std::is_void_v<tag_invoke_result_t<set_value_cpo, Receiver, Values...>>);
    return tag_invoke(*this, (Receiver &&) r, (Values &&) values...);
  }
} set_value{};

inline constexpr struct set_error_cpo {
  template <typename Receiver, typename Error>
  friend auto tag_invoke(set_error_cpo, Receiver&& r, Error&& e) noexcept
      -> decltype(static_cast<Receiver&&>(r).set_error((Error &&) e)) {
    static_assert(
        noexcept(static_cast<Receiver&&>(r).set_error((Error &&) e)),
        "receiver.set_error() method must be nothrow invocable");
    return static_cast<Receiver&&>(r).set_error((Error &&) e);
  }

  template <typename Receiver, typename Error>
  auto operator()(Receiver&& r, Error&& error) const noexcept
    -> tag_invoke_result_t<set_error_cpo, Receiver, Error> {
    static_assert(
        noexcept(tag_invoke(*this, (Receiver &&) r, (Error &&) error)),
        "set_error() invocation is required to be noexcept.");
    static_assert(
      std::is_void_v<tag_invoke_result_t<set_error_cpo, Receiver, Error>>
    );
    return tag_invoke(*this, (Receiver &&) r, (Error &&) error);
  }
} set_error{};

inline constexpr struct set_done_cpo {
  template <typename Receiver>
  friend auto tag_invoke(set_done_cpo, Receiver&& r) noexcept
      -> decltype(static_cast<Receiver&&>(r).set_done()) {
    static_assert(
        noexcept(static_cast<Receiver&&>(r).set_done()),
        "receiver.set_done() method must be nothrow invocable");
    return static_cast<Receiver&&>(r).set_done();
  }

  template <typename Receiver>
  auto operator()(Receiver&& r) const noexcept
      -> tag_invoke_result_t<set_done_cpo, Receiver> {
    static_assert(
        noexcept(tag_invoke(*this, (Receiver &&) r)),
        "set_done() invocation is required to be noexcept.");
    static_assert(std::is_void_v<tag_invoke_result_t<set_done_cpo, Receiver>>);
    return tag_invoke(*this, (Receiver &&) r);
  }
} set_done{};

template <typename T>
constexpr bool is_receiver_cpo_v = is_one_of_v<
    std::remove_cvref_t<T>,
    set_value_cpo,
    set_error_cpo,
    set_done_cpo>;

template <typename T>
struct is_receiver_cpo : std::bool_constant<is_receiver_cpo_v<T>> {};

} // namespace unifex
