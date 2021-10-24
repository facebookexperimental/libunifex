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
#include <unifex/blocking.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _just_void_or_done {

template <typename Receiver>
struct _op {
  struct type;
};
template <typename Receiver>
using operation = typename _op<remove_cvref_t<Receiver>>::type;

template <typename Receiver>
struct _op<Receiver>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  bool isVoid_;

  void start() & noexcept {
    if (isVoid_) {
      unifex::set_value((Receiver &&) receiver_);
    } else {
      unifex::set_done((Receiver &&) receiver_);
    }
  }
};

struct _sender {
  bool isVoid_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  // clang-format off
  UNIFEX_TEMPLATE(typename Receiver)
      (requires receiver<Receiver>)
  friend auto tag_invoke(tag_t<connect>, _sender s, Receiver&& r)
      noexcept(std::is_nothrow_move_constructible_v<Receiver>) {
    return operation<Receiver>{static_cast<Receiver&&>(r), s.isVoid_};
  }
  // clang-format on

  friend constexpr auto tag_invoke(tag_t<blocking>, const _sender&) noexcept {
    return blocking_kind::always_inline;
  }
};

inline constexpr struct just_void_or_done_fn {
  constexpr auto operator()(bool isVoid) const noexcept {
    return _sender{isVoid};
  }
} just_void_or_done{};
}  // namespace _just_void_or_done
using _just_void_or_done::just_void_or_done;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
