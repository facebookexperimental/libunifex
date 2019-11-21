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

#include <tuple>
#include <type_traits>

namespace unifex {

inline constexpr struct start_cpo {
  template <typename Operation>
  friend auto tag_invoke(start_cpo, Operation& op) noexcept(
      noexcept(op.start())) -> decltype(op.start()) {
    return op.start();
  }

  template <typename Operation>
  auto operator()(Operation& op) const noexcept
      -> tag_invoke_result_t<start_cpo, Operation&> {
    static_assert(
      std::is_void_v<tag_invoke_result_t<start_cpo, Operation&>>);
    static_assert(
        noexcept(tag_invoke(*this, op)),
        "start() customisation must be noexcept");
    return tag_invoke(*this, op);
  }
} start{};

inline constexpr struct connect_cpo {
  template <typename Sender, typename Receiver>
  friend auto tag_invoke(connect_cpo, Sender&& s, Receiver&& r) noexcept(
      noexcept(static_cast<Sender&&>(s).connect((Receiver &&) r)))
      -> decltype(static_cast<Sender&&>(s).connect((Receiver &&) r)) {
    return static_cast<Sender&&>(s).connect((Receiver &&) r);
  }

  template <typename Sender, typename Receiver>
  constexpr auto operator()(Sender&& sender, Receiver&& receiver) const
      noexcept(is_nothrow_tag_invocable_v<connect_cpo, Sender, Receiver>)
          -> tag_invoke_result_t<connect_cpo, Sender, Receiver> {
    return tag_invoke(*this, (Sender &&) sender, (Receiver &&) receiver);
  }
} connect{};

template <typename Sender, typename Receiver>
using operation_t = decltype(connect(
    std::declval<Sender>(),
    std::declval<Receiver>()));

template <typename Sender, typename Receiver>
inline constexpr bool is_connectable_v =
  std::is_invocable_v<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Receiver>
inline constexpr bool is_nothrow_connectable_v =
  std::is_nothrow_invocable_v<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Adaptor>
using adapt_error_types_t =
    typename Sender::template error_types<Adaptor::template apply>;

template <
    typename Sender,
    template <typename...> class Variant,
    typename Adaptor>
using adapt_value_types_t =
    typename Sender::template value_types<Variant, typename Adaptor::apply>;

template <typename... Values>
struct single_type {
  // empty so we are SFINAE friendly.
};

template <typename T>
struct single_type<T> {
  using type = T;
};

template <typename... Types>
struct single_value_type {
  using type = std::tuple<Types...>;
};

template <typename T>
struct single_value_type<T> {
  using type = T;
};

template <>
struct single_value_type<> {
  using type = void;
};

template <typename Sender>
using single_value_result_t = non_void_t<wrap_reference_t<decay_rvalue_t<
    typename Sender::template value_types<single_type, single_value_type>::
        type::type>>>;

template <typename Sender>
constexpr bool is_sender_nofail_v =
    Sender::template error_types<is_empty_list>::value;

} // namespace unifex
