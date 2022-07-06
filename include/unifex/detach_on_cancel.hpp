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

    void operator()() noexcept { state_->request_stop(); }
  };

  struct type final {
    type(UpstreamSender&& s, DownstreamReceiver&& r)
      : receiver_(std::move(r))
      , state_(std::make_unique<detached_state>(*this, (UpstreamSender &&) s)) {}

    friend void tag_invoke(tag_t<unifex::start>, type& op) noexcept {
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
  // use low 2 bits of type* as ref count
  static_assert(alignof(type) > 2);
};

template <typename UpstreamSender, typename DownstreamReceiver>
struct operation_state<UpstreamSender, DownstreamReceiver>::_receiver final {
  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    if (auto op = state_->try_get_op()) {
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
    if (auto op = state_->try_get_op()) {
      unifex::set_error(std::move(op->receiver_), (Error &&) error);
    }
  }

  void set_done() noexcept {
    if (auto op = state_->try_get_op()) {
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
struct operation_state<UpstreamSender, DownstreamReceiver>::detached_state final {
  using parent_op_t =
      typename operation_state<UpstreamSender, DownstreamReceiver>::type;

  detached_state(
      parent_op_t& op,
      UpstreamSender&& s)  //
      noexcept(is_nothrow_connectable_v<UpstreamSender, _receiver>)
    : parentOp_(init_refcount(op))
    , childOp_(connect(std::move(s), _receiver{this})) {}

  static constexpr std::uintptr_t mask = ~std::uintptr_t{3u};
  // operation_state::type* stored as integer: low 2 bits for ref count
  std::atomic_uintptr_t parentOp_;
  inplace_stop_source stopSource_;
  connect_result_t<UpstreamSender, _receiver> childOp_;

  void request_stop() noexcept {
    auto expected = parentOp_.load(std::memory_order_relaxed);
    if (ref_count(expected) == 0u) {
      // try_get_op already executed
      return;
    }
    UNIFEX_ASSERT(ref_count(expected) == 1u);
    // 1. set ref count to 2
    // 2. zero the pointer
    // if successful, callback owns the op and ptr remains in `expected`
    if (!parentOp_.compare_exchange_strong(
            expected,
            2u,
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      UNIFEX_ASSERT(ref_count(expected) == 0u);
      return;
    }
    stopSource_.request_stop();
    auto refCount = parentOp_.fetch_sub(1u, std::memory_order_acq_rel);
    UNIFEX_ASSERT(parent_op_ptr(refCount) == nullptr);
    auto op = parent_op_ptr(expected);
    op->callback_.destruct();
    if (refCount == 1u) {
      // callback owns the detached state
      op->state_.reset();
    } else {
      (void)op->state_.release();
    }
    unifex::set_done(std::move(op->receiver_));
  }

  parent_op_t* try_get_op() noexcept {
    auto op = parentOp_.fetch_sub(1u, std::memory_order_acq_rel);
    if (ref_count(op) != 1u) {
      // decrement from 2 means lost race with stop callback
      UNIFEX_ASSERT(ref_count(op) == 2u);
      UNIFEX_ASSERT(parent_op_ptr(op) == nullptr);
      return nullptr;
    }
    if (auto ptr = parent_op_ptr(op)) {
      ptr->callback_.destruct();
      return ptr;
    }
    // stop owns callback destruction
    delete this;
    return nullptr;
  }

private:
  // ref count in low 2 bits: start at 1
  static std::uintptr_t init_refcount(parent_op_t& parentOp) noexcept {
    return reinterpret_cast<std::uintptr_t>(&parentOp) | std::uintptr_t{1u};
  }

  static parent_op_t* parent_op_ptr(std::uintptr_t parentOp) noexcept {
    return reinterpret_cast<parent_op_t*>(parentOp & mask);
  }

  static std::uintptr_t ref_count(std::uintptr_t parentOp) noexcept {
    return parentOp & ~mask;
  }
};

template <typename Sender>
struct _sender {
  struct type;
};

template <typename Sender>
using sender = typename _sender<Sender>::type;

template <typename Sender>
struct _sender<Sender>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Sender upstreamSender_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Sender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Sender, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = true;

  friend constexpr auto tag_invoke(tag_t<blocking>, const type& sender) noexcept {
    if constexpr (same_as<blocking_kind,
                      decltype(blocking(sender.upstreamSender_))>) {
      // the sender returns a runtime-determined blocking_kind
      blocking_kind blockValue = blocking(sender.upstreamSender_);
      if (blockValue == blocking_kind::never) {
        blockValue = blocking_kind::maybe;
      }
      return blockValue;
    } else if constexpr (blocking_kind::never == cblocking<Sender>()) {
      // the sender always returns never
      return blocking_kind::maybe;
    } else {
      return cblocking<Sender>();
    }
  }

  template <typename This, typename Receiver>
  using operation_state_t =
      operation_state<member_t<This, Sender>, remove_cvref_t<Receiver>>;

  template(typename This, typename Receiver)  //
      (requires same_as<remove_cvref_t<This>, type> AND
           receiver<Receiver> AND  //
               sender_to<member_t<This, Sender>, //
                   typename operation_state_t<This, Receiver>::_receiver>)  //
  friend typename operation_state_t<This, Receiver>::type tag_invoke(
          tag_t<unifex::connect>, This&& s, Receiver&& r) noexcept(false) {
    return typename operation_state_t<This, Receiver>::type{
        static_cast<This&&>(s).upstreamSender_, static_cast<Receiver&&>(r)};
  }
};
}  // namespace _detach_on_cancel

namespace detach_on_cancel_impl {
inline constexpr struct detach_on_cancel_fn {
  template(typename Sender)(requires sender<Sender>) constexpr auto
  operator()(Sender&& sender) const
      noexcept(std::is_nothrow_constructible_v<_detach_on_cancel::sender<remove_cvref_t<Sender>>,
               Sender>) -> _detach_on_cancel::sender<remove_cvref_t<Sender>> {
    return _detach_on_cancel::sender<remove_cvref_t<Sender>>{(Sender &&)
                                                                 sender};
  }
} detach_on_cancel{};
}  // namespace detach_on_cancel_impl
using detach_on_cancel_impl::detach_on_cancel;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
