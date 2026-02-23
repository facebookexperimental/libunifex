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
#include <unifex/get_stop_token.hpp>
#include <unifex/just_done.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/stream_concepts.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _never {

template <bool CanSendVoid>
using maybe_bool = std::conditional_t<CanSendVoid, bool, std::in_place_t>;

template <typename Receiver, bool CanSendVoid>
struct _op {
  struct type;
};

template <typename Receiver, bool CanSendVoid>
using operation = typename _op<remove_cvref_t<Receiver>, CanSendVoid>::type;

template <typename Receiver, bool CanSendVoid>
struct _op<Receiver, CanSendVoid>::type {
  struct cancel_callback {
    type& op_;
    void operator()() noexcept {
      op_.stopCallback_.destruct();
      unifex::set_done(static_cast<Receiver&&>(op_.receiver_));
    }
  };

  using stop_token_type = stop_token_type_t<Receiver&>;

  static_assert(
      CanSendVoid || !is_stop_never_possible_v<stop_token_type>,
      "never should not be used with a stop-token "
      "type that can never be stopped.");
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  manual_lifetime<
      typename stop_token_type::template callback_type<cancel_callback>>
      stopCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS maybe_bool<CanSendVoid> isVoid_;

  template(typename Receiver2)                            //
      (requires constructible_from<Receiver, Receiver2>)  //
      type(Receiver2&& receiver, maybe_bool<CanSendVoid> isVoid) noexcept(
          std::is_nothrow_constructible_v<Receiver, Receiver2&&>)
    : receiver_((Receiver2&&)receiver)
    , isVoid_(isVoid) {}

  void start() & noexcept {
    if constexpr (CanSendVoid) {
      if (isVoid_) {
        unifex::set_value((Receiver&&)receiver_);
        return;
      }
    }

    UNIFEX_ASSERT(get_stop_token(receiver_).stop_possible());
    stopCallback_.construct(get_stop_token(receiver_), cancel_callback{*this});
  }
};

template <bool CanSendVoid>
struct sender {
  maybe_bool<CanSendVoid> isVoid_{};

  sender() = default;
  explicit constexpr sender(bool isVoid) noexcept {
    if constexpr (CanSendVoid) {
      isVoid_ = isVoid;
    }
  }

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types =
      std::conditional_t<CanSendVoid, Variant<Tuple<>>, Variant<>>;

  template <template <typename...> class Variant>
  using error_types =
      std::conditional_t<CanSendVoid, Variant<std::exception_ptr>, Variant<>>;

  static constexpr bool sends_done = true;

  // we'll complete inline if started with a stopped stop token or if isVoid_
  // is true
  static constexpr blocking_kind blocking = blocking_kind::maybe;

  // we complete wherever the stop callback is invoked, which must be on the
  // receiver's scheduler if scheduler affinity is required
  static constexpr bool is_always_scheduler_affine = true;

  template <typename Receiver>
  operation<Receiver, CanSendVoid> connect(Receiver&& receiver) const {
    return operation<Receiver, CanSendVoid>{(Receiver&&)receiver, isVoid_};
  }
};

explicit sender(bool) -> sender<true>;

struct stream {
  friend constexpr sender<false> tag_invoke(tag_t<next>, stream&) noexcept {
    return {};
  }
  friend constexpr auto tag_invoke(tag_t<cleanup>, stream&) noexcept {
    return just_done();
  }
};
}  // namespace _never

using never_sender = _never::sender<false>;
using never_stream = _never::stream;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
