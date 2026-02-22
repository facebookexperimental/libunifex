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

#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <unifex/detail/prologue.hpp>

#include <atomic>

namespace unifex {
namespace _cancellable {

template <typename NestedOp>
struct _op {
  enum state : uint8_t {
    stopped = 1,
    started = 2,
    completed = 4,
    non_stop = 8
  };

  struct non_stop_type : NestedOp {
    template <typename Sender, typename Receiver>
    explicit non_stop_type(
        Sender&& nested_sender,
        Receiver&& receiver,
        uint8_t state =
            non_stop) noexcept(is_nothrow_connectable_v<Sender, Receiver>)
      : NestedOp(
            unifex::connect(
                std::forward<decltype(nested_sender)>(nested_sender),
                std::forward<decltype(receiver)>(receiver),
                std::true_type{}))
      , state_(state) {}

    std::atomic<uint8_t> state_;
  };

  struct stop_type : non_stop_type {
    using non_stop_type::non_stop_type;

    void start() noexcept {
      unifex::start(*static_cast<NestedOp*>(this));

      if (auto state =
              this->state_.fetch_or(started, std::memory_order_acq_rel);
          state == stopped /* completed is not set! */) {
        NestedOp::stop();  // forward stop request after start() completes
      }
    }

    void stop() noexcept = delete;

    ~stop_type() {
      if (auto state = this->state_.load(std::memory_order_acquire);
          (state & completed) == 0 && (state & started) != 0) {
        (*this->cleanup_)(this);
      }
    }

    void (*cleanup_)(stop_type*) noexcept = [](stop_type*) noexcept {
    };
  };

  struct stop_callback {
    void operator()() noexcept {
      if (auto state = op_->state_.fetch_or(stopped, std::memory_order_acq_rel);
          state == started /* neither stopped nor completed are set! */) {
        op_->NestedOp::stop();
      }
    }

    stop_type* op_;
  };

  template <typename StopToken, bool StopsEarly>
  struct type;
};

template <typename NestedOp>
bool try_complete(NestedOp* self) noexcept {
  using op = _op<NestedOp>;

  auto state = static_cast<typename op::non_stop_type*>(self)->state_.fetch_or(
      op::completed, std::memory_order_acq_rel);

  if ((state & op::completed) != 0) {
    return false;
  }

  if ((state & op::non_stop) == 0) {
    auto* stop_self{static_cast<typename op::stop_type*>(self)};
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    // This line never executes if used with a non-stop token.
    (*stop_self->cleanup_)(stop_self);
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
  }

  return true;
}

template <typename NestedOp>
template <typename StopToken, bool StopsEarly>
struct _op<NestedOp>::type : _op<NestedOp>::stop_type {
  using op = _op<NestedOp>;
  using stop_callback_t =
      typename StopToken::template callback_type<op::stop_callback>;

  template <typename Sender, typename Receiver>
  explicit type(
      Sender&& nested_sender,
      Receiver&& receiver,
      StopToken&& token) noexcept(is_nothrow_connectable_v<Sender, Receiver>)
    : op::stop_type(
          std::forward<Sender>(nested_sender),
          std::forward<Receiver>(receiver),
          0) {
    stop_.template construct<StopToken>(std::forward<StopToken>(token));
  }

  void start() noexcept {
    auto token{stop_.template get<StopToken>()};
    stop_.template destruct<StopToken>();
    stop_.template construct<stop_callback_t>(token, op::stop_callback{this});

    this->cleanup_ = [](stop_type* self) noexcept {
      static_cast<type*>(self)->stop_.template destruct<stop_callback_t>();
    };

    if constexpr (StopsEarly) {
      if (this->state_.load(std::memory_order_acquire) == stopped) {
        NestedOp::stop();
        return;
      }
    }

    op::stop_type::start();
  }

  manual_lifetime_union<StopToken, stop_callback_t> stop_;
};

template <typename Sender, bool StopsEarly = false>
class cancellable : public sender_traits<Sender> {
public:
  explicit cancellable(
      Sender&& sender,
      std::bool_constant<StopsEarly> =
          {}) noexcept(std::is_nothrow_move_constructible_v<Sender>)
    : sender_(std::move(sender)) {}

  template <typename... Args>
  explicit cancellable(Args&&... args) noexcept(
      std::is_nothrow_constructible_v<Sender, Args...>)
    : sender_(std::forward<Args>(args)...) {}

  template(typename Receiver)                                           //
      (requires is_stop_never_possible_v<stop_token_type_t<Receiver>>)  //
      friend auto tag_invoke(
          unifex::tag_t<connect>,
          cancellable&& self,
          Receiver&&
              receiver) noexcept(is_nothrow_connectable_v<Sender, Receiver>) {
    using nested_op_t = raw_connect_result_t<Sender, Receiver>;
    return typename _op<nested_op_t>::non_stop_type{
        std::move(self.sender_), std::forward<Receiver>(receiver)};
  }

  template(typename Receiver)                                             //
      (requires(!is_stop_never_possible_v<stop_token_type_t<Receiver>>))  //
      friend auto tag_invoke(
          unifex::tag_t<connect>,
          cancellable&& self,
          Receiver&&
              receiver) noexcept(is_nothrow_connectable_v<Sender, Receiver>) {
    using nested_op_t = raw_connect_result_t<Sender, Receiver>;
    static_assert(
        std::is_nothrow_invocable_v<decltype(&nested_op_t::stop), nested_op_t*>,
        "operation must provide a nothrow stop() method");
    using op_type = typename _op<
        nested_op_t>::template type<stop_token_type_t<Receiver>, StopsEarly>;
    return op_type{
        std::move(self.sender_),
        std::forward<Receiver>(receiver),
        get_stop_token(receiver)};
  }

private:
  Sender sender_;
};

}  // namespace _cancellable

using _cancellable::cancellable;
using _cancellable::try_complete;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
