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

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

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
  bool is_void_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void start() & noexcept {
    if (is_void_) {
      unifex::set_value((Receiver &&) receiver_);
    } else {
      unifex::set_done((Receiver &&) receiver_);
    }
  }
};

struct _sender {
  bool is_void_;

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
  UNIFEX_TEMPLATE(typename This, typename Receiver)
      (requires same_as<remove_cvref_t<This>, _sender> UNIFEX_AND
          receiver<Receiver>)
  friend auto tag_invoke(tag_t<connect>, This&& that, Receiver&& r) noexcept {
    return operation<Receiver>{that.is_void_, static_cast<Receiver&&>(r)};
  }
  // clang-format on

  friend constexpr auto tag_invoke(tag_t<blocking>, const _sender&) noexcept {
    return blocking_kind::always_inline;
  }
};
}  // namespace _just_void_or_done

namespace _just_void_or_done_cpo {
inline const struct just_void_or_done_fn {
  constexpr auto operator()(bool is_void) const noexcept {
    return _just_void_or_done::_sender{is_void};
  }
} just_void_or_done{};
}  // namespace _just_void_or_done_cpo
using _just_void_or_done_cpo::just_void_or_done;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
