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

#include <unifex/unstoppable_token.hpp>
#include <unifex/with_query_value.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _unstoppable {

template <typename Sender>
struct _sender final {
  struct type;
};

template <typename Sender>
using sender = typename _sender<remove_cvref_t<Sender>>::type;

template <typename Sender>
struct _sender<Sender>::type final {
  UNIFEX_NO_UNIQUE_ADDRESS Sender sender_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Sender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Sender, Variant>;

  static constexpr bool sends_done = sender_traits<Sender>::sends_done;

  template(typename Self, typename Receiver)  //
      (requires same_as<type, remove_cvref_t<Self>> AND
           sender_to<member_t<Self, Sender>, Receiver>)  //
      friend auto tag_invoke(tag_t<connect>, Self&& self, Receiver&& r) noexcept(
          is_nothrow_connectable_v<
              member_t<Self, Sender>,
              remove_cvref_t<Receiver>>) {
    return connect(
        with_query_value(
            static_cast<Self&&>(self).sender_,
            get_stop_token,
            unstoppable_token{}),
        static_cast<Receiver&&>(r));
  }

  friend auto tag_invoke(tag_t<blocking>, const type& s) noexcept {
    return blocking(s.sender_);
  }
};

}  // namespace _unstoppable

namespace _unstoppable_cpo {
inline const struct _fn {
  template <typename Sender>
  constexpr auto operator()(Sender&& sender) const noexcept(
      std::is_nothrow_constructible_v<_unstoppable::sender<Sender>, Sender>) {
    return _unstoppable::sender<Sender>{static_cast<Sender&&>(sender)};
  }
} unstoppable{};
}  // namespace _unstoppable_cpo

using _unstoppable_cpo::unstoppable;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
