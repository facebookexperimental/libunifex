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
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/let_value_with_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/nest.hpp>
#include <unifex/on.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/spawn_detached.hpp>
#include <unifex/spawn_future.hpp>
#include <unifex/then.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/v2/async_scope.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex {

inline namespace v1 {

namespace _async_scope {

template <typename Receiver>
struct _attach_op_base final {
  struct type;
};

template <typename Receiver>
struct _attach_op_base<Receiver>::type {
UNIFEX_DIAGNOSTIC_PUSH
UNIFEX_INGORE_MAYBE_UNINITIALIZED_IN_GCC_9

  template <typename Receiver2>
  explicit type(inplace_stop_token stoken, Receiver2&& receiver) noexcept(
      std::is_nothrow_constructible_v<Receiver, Receiver2>)
    : stoken_(stoken)
    , receiver_(static_cast<Receiver2&&>(receiver)) {}

UNIFEX_DIAGNOSTIC_POP

  type(type&&) = delete;

  ~type() = default;

  void construct_stop_callbacks() noexcept {
    stokenCallback_.construct(stoken_, stop_callback{this});
    receiverCallback_.construct(get_stop_token(receiver_), stop_callback{this});
  }

  void request_stop() noexcept {
    // try to increment the refcount from 1 to 2
    std::size_t expected{1};
    if (!refcount_.compare_exchange_strong(
            expected, 2, std::memory_order_relaxed)) {
      // we didn't get to increment from one to two so either the count was
      // already zero because the operation is already complete, or the count
      // was two because we're the second stop callback; in either case, we
      // should just no-op
      UNIFEX_ASSERT(expected == 0 || expected == 2);

      return;
    }

    stopSource_.request_stop();

    if (auto receiver = try_complete()) {
      unifex::set_done(std::move(*receiver));
    }
  }

  Receiver* try_complete() noexcept {
    // decrement refcount and check the old count
    if (refcount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      // the old count was one so we've won the race to be the completer
      receiverCallback_.destruct();
      stokenCallback_.destruct();

      return &receiver_;
    }

    return nullptr;
  }

  struct stop_callback {
    type* op_;

    void operator()() noexcept { op_->request_stop(); }
  };

  using stoken_callback_t =
      typename inplace_stop_token::template callback_type<stop_callback>;

  using receiver_stoken_t = stop_token_type_t<Receiver>;
  using receiver_callback_t =
      typename receiver_stoken_t::template callback_type<stop_callback>;

  inplace_stop_token stoken_;
  std::atomic<std::size_t> refcount_{1};
  inplace_stop_source stopSource_;

  manual_lifetime<stoken_callback_t> stokenCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<receiver_callback_t>
      receiverCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
};

template <typename Sender, typename Receiver>
struct _attach_op final {
  struct type;
};

template <typename Receiver>
struct _attach_receiver final {
  struct type;
};

template <typename Receiver>
struct _attach_receiver<Receiver>::type final {
  template <typename... T>
  void set_value(T... values) noexcept {
    if (auto receiver = op_->try_complete()) {
      UNIFEX_TRY {
        unifex::set_value(std::move(*receiver), std::move(values)...);
      }
      UNIFEX_CATCH(...) {
        unifex::set_error(std::move(*receiver), std::current_exception());
      }
    }
  }

  template <typename E>
  void set_error(E e) noexcept {
    if (auto receiver = op_->try_complete()) {
      unifex::set_error(std::move(*receiver), std::move(e));
    }
  }

  void set_done() noexcept {
    if (auto receiver = op_->try_complete()) {
      unifex::set_done(std::move(*receiver));
    }
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
    return r.op_->stopSource_.get_token();
  }

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO&& cpo, const type& r) noexcept
      -> decltype(std::forward<CPO>(cpo)(std::declval<const Receiver&>())) {
    return std::forward<CPO>(cpo)(r.op_->receiver_);
  }

  typename _attach_op_base<Receiver>::type* op_;
};

template <typename Sender, typename Receiver>
struct _attach_op<Sender, Receiver>::type final
  : _attach_op_base<Receiver>::type {
  using base_t = typename _attach_op_base<Receiver>::type;

  using receiver_t = typename _attach_receiver<Receiver>::type;
  using op_t = connect_result_t<Sender, receiver_t>;

  template <typename Sender2, typename Receiver2>
  explicit type(
      inplace_stop_token stoken,
      Sender2&& sender,
      Receiver2&& receiver) noexcept(std::
                                         is_nothrow_constructible_v<
                                             base_t,
                                             inplace_stop_token&,
                                             Receiver2>&&
                                             is_nothrow_connectable_v<
                                                 Sender2,
                                                 receiver_t>)
    : base_t{stoken, static_cast<Receiver2&&>(receiver)}
    , op_(connect(static_cast<Sender2&&>(sender), receiver_t{this})) {}

  type(type&&) = delete;

  ~type() = default;

  friend void tag_invoke(tag_t<start>, type& op) noexcept {
    op.construct_stop_callbacks();
    unifex::start(op.op_);
  }

  op_t op_;
};

template <typename Sender>
struct _attach_sender final {
  struct type;
};

template <typename Sender>
struct _attach_sender<Sender>::type final {
  template <
      template <typename...>
      typename Variant,
      template <typename...>
      typename Tuple>
  using value_types = sender_value_types_t<Sender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Sender, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Sender>::sends_done;

  static constexpr blocking_kind blocking = std::min(
      blocking_kind::maybe(),
      sender_traits<Sender>::blocking());

  static constexpr bool is_always_scheduler_affine =
      sender_traits<Sender>::is_always_scheduler_affine;

  template <typename Sender2>
  explicit type(inplace_stop_token stoken, Sender2&& sender) noexcept(
      std::is_nothrow_constructible_v<Sender, Sender2>)
    : stoken_(stoken)
    , sender_(static_cast<Sender2&&>(sender)) {}

  template <typename Receiver>
  using op_t = typename _attach_op<Sender, remove_cvref_t<Receiver>>::type;

  template(typename Sender2, typename Receiver)          //
      (requires same_as<remove_cvref_t<Sender2>, type>)  //
      friend auto tag_invoke(
          tag_t<connect>,
          Sender2&& sender,
          Receiver&& receiver) noexcept(std::
                                            is_nothrow_constructible_v<
                                                op_t<Receiver>,
                                                inplace_stop_token,
                                                member_t<Sender2, Sender>,
                                                Receiver>) -> op_t<Receiver> {
    return op_t<Receiver>{
        sender.stoken_,
        static_cast<Sender2&&>(sender).sender_,
        static_cast<Receiver&&>(receiver)};
  }

  friend constexpr blocking_kind
  tag_invoke(tag_t<unifex::blocking>, [[maybe_unused]] const type& self) noexcept {
    if constexpr (sender_traits<Sender>::blocking == blocking_kind::always_inline) {
      // we can be constexpr in this case
      return blocking_kind::always_inline;
    }
    else {
      if (self.scope_) {
        return unifex::blocking(self.sender_);
      } else {
        // we complete inline with done when there's no scope
        return blocking_kind::always_inline;
      }
    }
  }

private:
  inplace_stop_token stoken_;
  UNIFEX_NO_UNIQUE_ADDRESS Sender sender_;
};

struct async_scope {
  /**
   * Connects and starts the given Sender, returning a future<> with which you
   * can observe the result.
   */
  template <typename Sender>
  auto spawn(Sender&& sender)
      -> decltype(spawn_future(static_cast<Sender&&>(sender), *this)) {
    return spawn_future(static_cast<Sender&&>(sender), *this);
  }

  /**
   * Equivalent to spawn(on((Scheduler&&) scheduler, (Sender&&)sender)).
   */
  template(typename Sender, typename Scheduler)  //
      (requires scheduler<Scheduler>)            //
      auto spawn_on(Scheduler&& scheduler, Sender&& sender)
          -> decltype(spawn(on((Scheduler &&) scheduler, (Sender &&) sender))) {
    return spawn(on((Scheduler &&) scheduler, (Sender &&) sender));
  }

  /**
   * Equivalent to spawn_on((Scheduler&&)scheduler, just_from((Fun&&)fun)).
   */
  template(typename Scheduler, typename Fun)             //
      (requires scheduler<Scheduler> AND callable<Fun>)  //
      auto spawn_call_on(Scheduler&& scheduler, Fun&& fun) {
    return spawn_on((Scheduler &&) scheduler, just_from((Fun &&) fun));
  }

  /**
   * Connects and starts the given Sender with no way to observe the result.
   *
   * Invokes std::terminate() if the resulting operation completes with an
   * error.
   */
  template <typename Sender>
  auto detached_spawn(Sender&& sender)
      -> decltype(spawn_detached(static_cast<Sender&&>(sender), *this)) {
    spawn_detached(static_cast<Sender&&>(sender), *this);
  }

  /**
   * Equivalent to detached_spawn(on((Scheduler&&) scheduler,
   * (Sender&&)sender)).
   */
  template(typename Sender, typename Scheduler)  //
      (requires scheduler<Scheduler>)            //
      auto detached_spawn_on(Scheduler&& scheduler, Sender&& sender)
          -> decltype(detached_spawn(
              on((Scheduler &&) scheduler, (Sender &&) sender))) {
    detached_spawn(on((Scheduler &&) scheduler, (Sender &&) sender));
  }

  /**
   * Equivalent to detached_spawn_on((Scheduler&&)scheduler,
   * just_from((Fun&&)fun)).
   */
  template(typename Scheduler, typename Fun)             //
      (requires scheduler<Scheduler> AND callable<Fun>)  //
      void detached_spawn_call_on(Scheduler&& scheduler, Fun&& fun) {
    static_assert(
        is_nothrow_callable_v<Fun>,
        "Please annotate your callable with noexcept.");

    detached_spawn_on((Scheduler &&) scheduler, just_from((Fun &&) fun));
  }

  /**
   * Returns a _Sender_ that, when connected and started,
   * connects and starts the given Sender.
   *
   * Returned _Sender_ owns a reference to this async_scope.
   */
  template <typename Sender>
  [[nodiscard]] auto attach(Sender&& sender) noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Sender>, Sender>) {
    using attach_sender_t =
        typename _attach_sender<remove_cvref_t<Sender>>::type;

    return nest(
        attach_sender_t{stopSource_.get_token(), static_cast<Sender&&>(sender)},
        scope_);
  }

  /**
   * Equivalent to attach(just_from((Fun&&)fun)).
   */
  template(typename Fun)        //
      (requires callable<Fun>)  //
      [[nodiscard]] auto attach_call(Fun&& fun) noexcept(
          noexcept(attach(just_from((Fun &&) fun)))) {
    return attach(just_from((Fun &&) fun));
  }

  /**
   * Equivalent to attach(on((Scheduler&&) scheduler, (Sender&&)sender)).
   */
  template(typename Sender, typename Scheduler)           //
      (requires scheduler<Scheduler> AND sender<Sender>)  //
      [[nodiscard]] auto attach_on(Scheduler&& scheduler, Sender&& sender) noexcept(
          noexcept(attach(on((Scheduler &&) scheduler, (Sender &&) sender)))) {
    return attach(on((Scheduler &&) scheduler, (Sender &&) sender));
  }

  /**
   * Equivalent to attach_on((Scheduler&&)scheduler, just_from((Fun&&)fun)).
   */
  template(typename Scheduler, typename Fun)             //
      (requires scheduler<Scheduler> AND callable<Fun>)  //
      [[nodiscard]] auto attach_call_on(Scheduler&& scheduler, Fun&& fun) noexcept(
          noexcept(
              attach_on((Scheduler &&) scheduler, just_from((Fun &&) fun)))) {
    return attach_on((Scheduler &&) scheduler, just_from((Fun &&) fun));
  }

  /**
   * Returns a Sender that, when connected and started, marks the scope so that
   * no more work can be spawned within it.  The returned Sender completes when
   * the last outstanding operation spawned within the scope completes.
   */
  [[nodiscard]] auto complete() noexcept { return scope_.join(); }

  /**
   * Returns a Sender that, when connected and started, marks the scope so that
   * no more work can be spawned within it and requests cancellation of all
   * outstanding work.  The returned Sender completes when the last outstanding
   * operation spawned within the scope completes.
   *
   * Equivalent to, but more efficient than, invoking request_stop() and then
   * connecting and starting the result of complete().
   */
  [[nodiscard]] auto cleanup() noexcept {
    return sequence(
        just_from([this]() noexcept { request_stop(); }), scope_.join());
  }

  /**
   * Returns a stop token from the scope's internal stop source.
   */
  inplace_stop_token get_stop_token() noexcept {
    return stopSource_.get_token();
  }

  /**
   * Marks the scope so that no more work can be spawned within it and requests
   * cancellation of all outstanding work.
   */
  void request_stop() noexcept {
    scope_.end_scope();
    stopSource_.request_stop();
  }

private:
  inplace_stop_source stopSource_;
  unifex::v2::async_scope scope_;

  template(typename Sender, typename Scope)     //
      (requires same_as<async_scope&, Scope&>)  //
      friend auto tag_invoke(
          tag_t<nest>,
          Sender&& sender,
          Scope& scope) noexcept(noexcept(scope
                                              .attach(static_cast<Sender&&>(
                                                  sender))))
          -> decltype(scope.attach(static_cast<Sender&&>(sender))) {
    return scope.attach(static_cast<Sender&&>(sender));
  }
};

}  // namespace _async_scope

using v1::_async_scope::async_scope;

template <typename... Ts>
using future = unifex::v2::future<unifex::v1::async_scope, Ts...>;

}  // namespace v1

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
