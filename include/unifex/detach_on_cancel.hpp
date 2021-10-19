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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_list.hpp>
#include <atomic>
#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _detach_on_cancel {

template <typename UpstreamSender, typename DownstreamReceiver>
struct operation_state {
  struct _receiver;

  struct detached_state;

  struct cancel_callback final {
    detached_state* state_;

    void operator()() noexcept {
      if (state_ != nullptr) {
        state_->stopSource_.request_stop();
      }
      auto* op = state_->parentOp_.exchange(nullptr, std::memory_order_acq_rel);
      if (op != nullptr) {
        // We won the race for cancellation
        // ownership is transferred to the detached state
        (void)op->state_.release();
        op->callback_.destruct();
        unifex::set_done(std::move(op->receiver_));
      }
    }
  };

  struct type final {
    type(UpstreamSender&& s, DownstreamReceiver&& r)
      : receiver_(std::move(r))
      , state_(std::make_unique<detached_state>(this, (UpstreamSender &&) s)){};

    friend void tag_invoke(
        tag_t<unifex::start>,
        typename operation_state<UpstreamSender, DownstreamReceiver>::type&
            op) noexcept {
      auto& childOp = op.state_->childOp_;
      op.callback_.construct(
          get_stop_token(op.receiver_), cancel_callback{op.state_.get()});
      unifex::start(childOp);
    }

    remove_cvref_t<DownstreamReceiver> receiver_;
    manual_lifetime<typename stop_token_type_t<
        DownstreamReceiver>::template callback_type<cancel_callback>>
        callback_;
    std::unique_ptr<detached_state> state_;
  };
};

template <typename UpstreamSender, typename DownstreamReceiver>
struct operation_state<UpstreamSender, DownstreamReceiver>::_receiver final {
  auto tryGetOp() noexcept {
    // if cancellation won, parentOp_ would be null at this point
    // see operation_state::cancel_callback()
    auto* op = state_->parentOp_.exchange(nullptr, std::memory_order_acq_rel);
    if (op != nullptr) {
      op->callback_.destruct();
    } else {
      delete state_;
    }
    return op;
  }

  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    if (auto op = tryGetOp()) {
      UNIFEX_TRY {
        unifex::set_value(std::move(op->receiver_), (Values &&) values...);
      }
      UNIFEX_CATCH(...) {
        unifex::set_error(std::move(op->receiver_), std::current_exception());
      }
    }
  }

  template <typename Error>
  void set_error(Error&& error) noexcept {
    if (auto op = tryGetOp()) {
      unifex::set_error(std::move(op->receiver_), (Error &&) error);
    }
  }

  void set_done() noexcept {
    if (auto op = tryGetOp()) {
      unifex::set_done(std::move(op->receiver_));
    }
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const _receiver& r) noexcept {
    return r.state_->stopSource_.get_token();
  }
  detached_state* state_;
};

template <typename UpstreamSender, typename DownstreamReceiver>
struct operation_state<UpstreamSender, DownstreamReceiver>::detached_state
    final {
  detached_state(
      typename operation_state<UpstreamSender, DownstreamReceiver>::type* op,
      UpstreamSender&&
          s) noexcept(is_nothrow_connectable_v<UpstreamSender, _receiver>)
    : parentOp_(op)
    , childOp_(connect(std::move(s), _receiver{this})){};

  std::atomic<
      typename operation_state<UpstreamSender, DownstreamReceiver>::type*>
      parentOp_;
  inplace_stop_source stopSource_;
  connect_result_t<UpstreamSender, _receiver> childOp_;
};

template <typename Sender>
struct _sender {
  struct type;
};

template <typename Sender>
using sender = typename _sender<Sender>::type;

template <typename Sender>
struct _sender<Sender>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Sender upstream_sender_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types =
      typename sender_traits<Sender>::template value_types<Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<
          typename sender_traits<Sender>::template error_types<Variant>,
          type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = true;

  friend constexpr auto
  tag_invoke(tag_t<blocking>, const type& sender) noexcept {
    auto block_value = blocking(sender.upstream_sender_);
    if (block_value == _block::_enum::never) {
      return _block::_enum::maybe;
    }
    return block_value;
  }

  template(typename This, typename Receiver)(
      requires same_as<remove_cvref_t<This>, type> AND receiver<
          Receiver>) friend auto tag_invoke(tag_t<unifex::connect>, This&& s, Receiver&& r) noexcept(false) {
    return typename operation_state<Sender, Receiver>::type{
        ((This &&) s).upstream_sender_, (Receiver &&) r};
  }
};
}  // namespace _detach_on_cancel

namespace detach_on_cancel_impl {
inline constexpr struct detach_on_cancel_fn {
  template(typename Sender)(requires sender<Sender>) constexpr auto
  operator()(Sender&& sender) const
      noexcept(is_nothrow_constructible_v<
               _detach_on_cancel::sender<remove_cvref_t<Sender>>,
               Sender>) -> _detach_on_cancel::sender<remove_cvref_t<Sender>> {
    return _detach_on_cancel::sender<remove_cvref_t<Sender>>{(Sender &&)
                                                                 sender};
  }
} detach_on_cancel{};
}  // namespace detach_on_cancel_impl
using detach_on_cancel_impl::detach_on_cancel;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
