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
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/stream_concepts.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _never {

template <typename Receiver>
struct _op {
  struct type;
};
template <typename Receiver>
using operation = typename _op<remove_cvref_t<Receiver>>::type;

template <typename Receiver>
struct _op<Receiver>::type {
  struct cancel_callback {
    type& op_;
    void operator()() noexcept {
      op_.stopCallback_.destruct();
      unifex::set_done(static_cast<Receiver&&>(op_.receiver_));
    }
  };

  using stop_token_type = stop_token_type_t<Receiver&>;

  static_assert(
      !is_stop_never_possible_v<stop_token_type>,
      "never should not be used with a stop-token "
      "type that can never be stopped.");

  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  manual_lifetime<
      typename stop_token_type::
          template callback_type<cancel_callback>>
    stopCallback_;

  template(typename Receiver2)
    (requires constructible_from<Receiver, Receiver2>)
  type(Receiver2&& receiver)
    : receiver_((Receiver2 &&) receiver) {}

  void start() noexcept {
    UNIFEX_ASSERT(get_stop_token(receiver_).stop_possible());
    stopCallback_.construct(
        get_stop_token(receiver_), cancel_callback{*this});
  }
};

struct sender {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  template <typename Receiver>
  operation<Receiver> connect(Receiver&& receiver) {
    return operation<Receiver>{(Receiver &&) receiver};
  }
};

struct stream {
  friend constexpr sender tag_invoke(tag_t<next>, stream&) noexcept {
    return {};
  }
  friend constexpr ready_done_sender tag_invoke(tag_t<cleanup>, stream&) noexcept {
    return {};
  }
};
} // namespace _never

using never_sender = _never::sender;
using never_stream = _never::stream;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
