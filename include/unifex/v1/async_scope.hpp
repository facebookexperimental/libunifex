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

#include <atomic>
#include <memory>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex {

inline namespace v1 {

namespace _async_scope {

struct async_scope;

void record_done(async_scope*) noexcept;

[[nodiscard]] bool try_record_start(async_scope* scope) noexcept;

inplace_stop_token get_stop_token_from_scope(async_scope*) noexcept;

template <typename Operation, typename Receiver>
struct _cleaning_receiver final {
  struct type;
};

template <typename Operation, typename Receiver>
using cleaning_receiver =
    typename _cleaning_receiver<Operation, Receiver>::type;

template <typename Operation, typename Receiver>
struct _cleaning_receiver<Operation, Receiver>::type final {
  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    op_->deliver_result([&](auto&& r) noexcept {
      UNIFEX_TRY { unifex::set_value(std::move(r), (Values &&) values...); }
      UNIFEX_CATCH(...) {
        unifex::set_error(std::move(r), std::current_exception());
      }
    });
  }

  template <typename E>
  void set_error(E&& e) noexcept {
    op_->deliver_result(
        [&](auto&& r) noexcept { unifex::set_error(std::move(r), (E &&) e); });
  }

  void set_done() noexcept {
    op_->deliver_result(
        [&](auto&& r) noexcept { unifex::set_done(std::move(r)); });
  }

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return static_cast<CPO&&>(cpo)(r.op_->get_receiver());
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
    return r.op_->get_token();
  }

  Operation* op_;
};

template <typename Sender, typename Receiver>
struct _attached_op final {
  class type;
};

template <typename Sender, typename Receiver>
using attached_operation =
    typename _attached_op<Sender, remove_cvref_t<Receiver>>::type;

template <typename Sender, typename Receiver>
class _attached_op<Sender, Receiver>::type final {
  using cleaning_receiver_t = cleaning_receiver<type, Receiver>;
  using nested_operation_t = connect_result_t<Sender, cleaning_receiver_t>;

  struct stop_callback {
    void operator()() noexcept { op_.request_stop(); }

    type& op_;
  };

public:
  template <typename Receiver2>
  explicit type(Sender&& s, Receiver2&& r, async_scope* scope) noexcept(
      is_nothrow_connectable_v<Sender, cleaning_receiver_t>&&
          std::is_nothrow_constructible_v<cleaning_receiver_t, type*>&&
              std::is_nothrow_constructible_v<Receiver, Receiver2>)
    : scope_(init_refcount(scope))
    , receiver_((Receiver2 &&) r) {
    if (scope) {
      op_.construct_with([&, this] {
        return unifex::connect((Sender &&) s, cleaning_receiver_t{this});
      });
    }
  }

  type(type&& op) = delete;

  ~type() {
    auto scope = scope_.load(std::memory_order_relaxed);
    if (scope != 0u) {
      op_.destruct();
      // started => receiver responsible for cleanup
      if (ref_count(scope) != 0u) {
        UNIFEX_ASSERT(ref_count(scope) == 1u);
        record_done(scope_ptr(scope));
      }
    }
  }

  void request_stop() noexcept {
    // increment from 0 means lost race with a completion method so
    // no-op and let the in-flight completion complete
    auto scope = scope_.load(std::memory_order_relaxed);
    auto expected = (scope & mask) | 1u;
    if (!scope_.compare_exchange_strong(
            expected, expected + 1u, std::memory_order_relaxed)) {
      return;
    }
    stopSource_.request_stop();
    deliver_result([](auto&& r) noexcept { unifex::set_done(std::move(r)); });
  }

  // reduce size of the templated `deliver_result`
  async_scope* deliver_result_prelude() noexcept {
    auto scope = scope_.fetch_sub(1u, std::memory_order_acq_rel);
    if (ref_count(scope) != 1u) {
      UNIFEX_ASSERT(ref_count(scope) == 2u);
      return nullptr;
    }
    UNIFEX_ASSERT(scope_ptr(scope) != nullptr);
    deregister_callbacks();
    return scope_ptr(scope);
  }

  template <typename Func>
  void deliver_result(Func func) noexcept {
    auto scope = deliver_result_prelude();
    if (scope) {
      func(std::move(receiver_));
      record_done(scope);
    }
  }

  auto get_token() noexcept { return stopSource_.get_token(); }

  const auto& get_receiver() const noexcept { return receiver_; }

  void deregister_callbacks() noexcept {
    receiverCallback_.destruct();
    scopeCallback_.destruct();
  }

  void register_callbacks() noexcept {
    receiverCallback_.construct(
        get_stop_token(receiver_), stop_callback{*this});
    scopeCallback_.construct(
        scope_ref()->get_stop_token(), stop_callback{*this});
  }

  friend void tag_invoke(tag_t<start>, type& op) noexcept {
    if (op.scope_ref()) {
      // TODO: callback constructor may throw
      // 1. catch and propagate with `set_error`
      // 2. careful when destroying callbacks (0, 1, 2?)
      op.register_callbacks();
      unifex::start(op.op_.get());
    } else {
      unifex::set_done(std::move(op).receiver_);
    }
  }

private:
  static constexpr std::uintptr_t mask = ~(1u | std::uintptr_t{1u << 1});
  // async_scope* stored as integer: low 2 bits for ref count
  std::atomic_uintptr_t scope_;
  using receiver_stop_token_t = stop_token_type_t<Receiver>;
  using scope_stop_token_t = inplace_stop_token;
  template <typename StopToken>
  using stop_callback_t = manual_lifetime<
      typename StopToken::template callback_type<stop_callback>>;
  UNIFEX_NO_UNIQUE_ADDRESS inplace_stop_source stopSource_;
  UNIFEX_NO_UNIQUE_ADDRESS stop_callback_t<receiver_stop_token_t>
      receiverCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS stop_callback_t<scope_stop_token_t> scopeCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<nested_operation_t> op_;

  async_scope* scope_ref() const noexcept {
    return scope_ptr(scope_.load(std::memory_order_relaxed));
  }

  // ref count in low 2 bits: 1 unless scope is null
  static std::uintptr_t init_refcount(async_scope* scope) noexcept {
    return reinterpret_cast<std::uintptr_t>(scope) |
        static_cast<std::uintptr_t>(static_cast<bool>(scope));
  }

  static async_scope* scope_ptr(std::uintptr_t scope) noexcept {
    return reinterpret_cast<async_scope*>(scope & mask);
  }

  static std::uintptr_t ref_count(std::uintptr_t scope) noexcept {
    return scope & ~mask;
  }
};

template <typename Sender>
struct _attached_sender final {
  class type;
};
template <typename Sender>
using attached_sender = typename _attached_sender<remove_cvref_t<Sender>>::type;

template <typename Sender>
class _attached_sender<Sender>::type final {
public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Sender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Sender, Variant>;

  static constexpr bool sends_done = sender_traits<Sender>::sends_done;

  template <typename Sender2>
  explicit type(Sender2&& sender, async_scope* scope) noexcept(
      std::is_nothrow_constructible_v<Sender, Sender2>)
    : scope_(scope)
    , sender_(static_cast<Sender2&&>(sender)) {
    try_attach();
  }

  type(const type& t) noexcept(std::is_nothrow_copy_constructible_v<Sender>)
    : scope_(t.scope_)
    , sender_(t.sender_) {
    try_attach();
  }

  type(type&& t) noexcept(std::is_nothrow_move_constructible_v<Sender>)
    : scope_(std::exchange(t.scope_, nullptr))
    , sender_(std::move(t.sender_)) {}

  ~type() {
    if (scope_) {
      record_done(scope_);
    }
  }

  type& operator=(type rhs) noexcept {
    std::swap(scope_, rhs.scope_);
    sender_ = std::move(rhs.sender_);
    return *this;
  }

  template(typename Receiver)        //
      (requires receiver<Receiver>)  //
      friend auto tag_invoke(tag_t<connect>, type&& s, Receiver&& r) noexcept(
          std::is_nothrow_constructible_v<
              attached_operation<Sender, Receiver>,
              Sender,
              Receiver,
              async_scope*>) -> attached_operation<Sender, Receiver> {
    const auto scope = std::exchange(s.scope_, nullptr);
    return attached_operation<Sender, Receiver>{
        static_cast<type&&>(s).sender_, static_cast<Receiver&&>(r), scope};
  }

  friend constexpr auto
  tag_invoke(tag_t<unifex::blocking>, const type& self) noexcept {
    return blocking(self.sender_);
  }

private:
  async_scope* scope_;
  UNIFEX_NO_UNIQUE_ADDRESS Sender sender_;

  void try_attach() noexcept {
    if (scope_) {
      if (!try_record_start(scope_)) {
        scope_ = nullptr;
      }
    }
  }
};

struct async_scope {
private:
  template <typename Scheduler, typename Sender>
  using _on_result_t =
      decltype(on(UNIFEX_DECLVAL(Scheduler &&), UNIFEX_DECLVAL(Sender&&)));

  inplace_stop_source stopSource_;
  // (opState_ & 1) is 1 until we've been stopped
  // (opState_ >> 1) is the number of outstanding operations
  std::atomic<std::size_t> opState_{1};
  async_manual_reset_event evt_;

  [[nodiscard]] auto await_and_sync() noexcept {
    return then(evt_.async_wait(), [this]() noexcept {
      // make sure to synchronize with all the fetch_subs being done while
      // operations complete
      (void)opState_.load(std::memory_order_acquire);
    });
  }

public:
  async_scope() noexcept = default;

  ~async_scope() {
    [[maybe_unused]] auto state = opState_.load(std::memory_order_relaxed);

    UNIFEX_ASSERT(is_stopping(state));
    UNIFEX_ASSERT(op_count(state) == 0);
  }

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
  [[nodiscard]] auto
  attach(Sender&& sender) noexcept(std::is_nothrow_constructible_v<
                                   attached_sender<Sender>,
                                   Sender,
                                   async_scope*>) {
    return attached_sender<Sender>{static_cast<Sender&&>(sender), this};
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
  [[nodiscard]] auto complete() noexcept {
    return sequence(
        just_from([this]() noexcept { end_of_scope(); }), await_and_sync());
  }

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
        just_from([this]() noexcept { request_stop(); }), await_and_sync());
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
    end_of_scope();
    stopSource_.request_stop();
  }

private:
  static constexpr std::size_t stoppedBit{1};

  /**
   * Returns true if the given state is marked with "stopping", indicating that
   * no more work may be spawned within the scope.
   */
  static bool is_stopping(std::size_t state) noexcept {
    return (state & stoppedBit) == 0;
  }

  /**
   * Returns the number of outstanding operations in the scope.
   */
  static std::size_t op_count(std::size_t state) noexcept { return state >> 1; }

  /**
   * Tries to record the start of a new operation, returning true on success.
   *
   * Returns false if the scope has been marked as not accepting new work.
   */
  [[nodiscard]] friend bool try_record_start(async_scope* scope) noexcept {
    auto opState = scope->opState_.load(std::memory_order_relaxed);

    do {
      if (is_stopping(opState)) {
        return false;
      }

      UNIFEX_ASSERT(opState + 2 > opState);
    } while (!scope->opState_.compare_exchange_weak(
        opState, opState + 2, std::memory_order_relaxed));

    return true;
  }

  /**
   * Records the completion of one operation.
   */
  friend void record_done(async_scope* scope) noexcept {
    auto oldState = scope->opState_.fetch_sub(2, std::memory_order_release);

    if (is_stopping(oldState) && op_count(oldState) == 1) {
      // the scope is stopping and we're the last op to finish
      scope->evt_.set();
    }
  }

  friend inplace_stop_token
  get_stop_token_from_scope(async_scope* scope) noexcept {
    return scope->get_stop_token();
  }

  /**
   * Marks the scope to prevent spawn from starting any new work.
   */
  void end_of_scope() noexcept {
    // stop adding work
    auto oldState = opState_.fetch_and(~stoppedBit, std::memory_order_release);

    if (op_count(oldState) == 0) {
      // there are no outstanding operations to wait for
      evt_.set();
    }
  }

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
// use low 2 bits of async_scope* as ref count
static_assert(alignof(unifex::v1::async_scope) > 2);

template <typename... Ts>
using future = unifex::v2::future<unifex::v1::async_scope, Ts...>;

}  // namespace v1

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
