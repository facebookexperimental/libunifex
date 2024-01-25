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

#include <tuple>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _stop_on_request {

// These states allow us to keep track of when the stop callbacks are created
// and called
enum class _callback_state : char {
  INIT = 0,
  ALL_CONSTRUCTED_NOT_CALLED = 1,
  AT_LEAST_ONE_CALLED = 2,
};

template <typename Receiver, typename... StopTokens>
struct _op {
  struct type;
};
template <typename Receiver, typename... StopTokens>
using operation = typename _op<remove_cvref_t<Receiver>, StopTokens...>::type;

template <typename Receiver, typename... StopTokens>
struct _op<Receiver, StopTokens...>::type {
  struct cancel_callback {
    type& op_;
    void operator()() noexcept { op_.request_stop(); }
  };

  void request_stop() noexcept {
    // update state to mark that at least one callback has been called
    auto oldState = callbackState_.exchange(
        _callback_state::AT_LEAST_ONE_CALLED, std::memory_order_acq_rel);

    if (oldState == _callback_state::ALL_CONSTRUCTED_NOT_CALLED) {
      // if this is the first callback that is called, then we can complete
      complete();
    } else {
      // otherwise, it means that either
      // 1. a stop callback was already called, so let that callback clean up or
      // 2. we're still constructing the callbacks, so let start() handle the
      // completion
      UNIFEX_ASSERT(
          oldState == _callback_state::AT_LEAST_ONE_CALLED ||
          oldState == _callback_state::INIT);
    }
  }

  void complete() noexcept {
    std::apply(
        [&](auto&... callbacks) noexcept { (callbacks.destruct(), ...); },
        stopCallbacks_);
    receiverStopCallback_.destruct();
    unifex::set_done(std::move(receiver_));
  }

  using stop_token_type = stop_token_type_t<Receiver&>;

  // All external stop tokens must be stoppable
  static_assert(
      (... && !is_stop_never_possible_v<StopTokens>),
      "stop_on_request should not be used with any stop-token "
      "types that can never be stopped.");

  // If no external stop tokens are provided, then the receiver must be
  // stoppable
  static_assert(
      (sizeof...(StopTokens) > 0 || !is_stop_never_possible_v<stop_token_type>),
      "stop_on_request should not be used with an unstoppable receiver "
      "if no stop-tokens are provided.");

  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<StopTokens...> stopTokens_;
  template <typename StopToken>
  using callback_t = manual_lifetime<
      typename StopToken::template callback_type<cancel_callback>>;

  UNIFEX_NO_UNIQUE_ADDRESS callback_t<stop_token_type> receiverStopCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<callback_t<StopTokens>...> stopCallbacks_;

  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  std::atomic<_callback_state> callbackState_{_callback_state::INIT};

  template(typename Receiver2)                            //
      (requires constructible_from<Receiver, Receiver2>)  //
      type(Receiver2&& receiver, std::tuple<StopTokens...> stopTokens)
    : stopTokens_(std::move(stopTokens))
    , receiver_(std::forward<Receiver2>(receiver)) {}

  template <size_t index = 0>
  constexpr void constructCallbacks() {
    if constexpr (std::tuple_size<decltype(stopTokens_)>::value == index) {
      // hit the end of the tuple, exit
      return;
    } else {
      std::get<index>(stopCallbacks_)
          .construct(std::get<index>(stopTokens_), cancel_callback{*this});
      scope_guard destructOnError = [this]() noexcept {
        // If the following callback failed to construct, then we need to clean
        // up the current callback, eventually cleaning up all of the callbacks
        // that have been constructed so far
        std::get<index>(stopCallbacks_).destruct();
      };

      constructCallbacks<index + 1>();

      destructOnError.release();
    }
  }

  void start() noexcept {
    UNIFEX_ASSERT(
        sizeof...(StopTokens) > 0 || get_stop_token(receiver_).stop_possible());

    UNIFEX_TRY {
      // construct a callback for the receiver stop token
      receiverStopCallback_.construct(
          get_stop_token(receiver_), cancel_callback{*this});

      scope_guard destructOnError = [this]() noexcept {
        receiverStopCallback_.destruct();
      };

      // construct a callback for each stop token
      // Note: if any of these tokens are already stopped, then its
      // corresponding callback will be immediately invoked
      constructCallbacks();

      destructOnError.release();
    }
    UNIFEX_CATCH(...) {
      if (callbackState_.load(std::memory_order_acquire) ==
          _callback_state::AT_LEAST_ONE_CALLED) {
        // we received a stop request before we experienced an error
        unifex::set_done(std::move(receiver_));
      } else {
        unifex::set_error(std::move(receiver_), std::current_exception());
      }

      return;
    }

    auto expected = _callback_state::INIT;
    // Update state to mark that all callbacks have been constructed only if the
    // previous state was INIT. If the previous state was AT_LEAST_ONE_CALLED,
    // don't change the state
    if (!callbackState_.compare_exchange_strong(
            expected,
            _callback_state::ALL_CONSTRUCTED_NOT_CALLED,
            std::memory_order_acq_rel)) {
      // complete if we transitioned from AT_LEAST_ONE_CALLED
      complete();
    }
    // otherwise let the other callbacks handle the completion
  }
};

template <typename... StopTokens>
struct _sender final {
  struct type;
};

template <typename... StopTokens>
using sender = typename _sender<StopTokens...>::type;

template <typename... StopTokens>
struct _sender<StopTokens...>::type final {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  // we'll complete inline if started with a stopped stop token
  static constexpr blocking_kind blocking = blocking_kind::maybe;

  // we complete wherever the stop callback is invoked, which may not be the
  // thread we started on
  static constexpr bool is_always_scheduler_affine = false;

  template(typename Self, typename Receiver)          //
      (requires same_as<remove_cvref_t<Self>, type>)  //
      friend operation<Receiver, StopTokens...> tag_invoke(
          tag_t<connect>,
          Self&& self,
          Receiver&&
              receiver) noexcept(std::
                                     is_nothrow_constructible_v<
                                         operation<Receiver, StopTokens...>,
                                         Receiver,
                                         member_t<
                                             Self,
                                             std::tuple<StopTokens...>>>) {
    return operation<Receiver, StopTokens...>{
        std::forward<Receiver>(receiver), std::forward<Self>(self).stopTokens_};
  }

  explicit type(StopTokens... stopTokens) noexcept(
      std::is_nothrow_constructible_v<std::tuple<StopTokens...>, StopTokens...>)
    : stopTokens_{std::move(stopTokens)...} {}

private:
  std::tuple<StopTokens...> stopTokens_;
};

struct _fn {
  template <typename... StopTokens>
  auto operator()(StopTokens... stopTokens) const
      noexcept(std::is_nothrow_constructible_v<
               _stop_on_request::sender<StopTokens...>,
               StopTokens...>) {
    return _stop_on_request::sender<StopTokens...>{std::move(stopTokens)...};
  }
};
}  // namespace _stop_on_request

inline constexpr _stop_on_request::_fn stop_on_request{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
