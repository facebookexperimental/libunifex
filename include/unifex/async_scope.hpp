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
#include <unifex/on.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/then.hpp>
#include <unifex/type_traits.hpp>

#include <atomic>
#include <memory>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _async_scope {

struct async_scope;

void record_done(async_scope*) noexcept;

enum class op_state : unsigned int { incomplete, done, value, error };

struct _spawn_op_base {
  struct stop_callback final {
    _spawn_op_base* self;

    void operator()() noexcept {
      self->request_stop();
    }
  };

  using stop_callback_t = typename inplace_stop_token::template callback_type<
      stop_callback>;

  using cleanup_t = void(*)(void*) noexcept;

  explicit _spawn_op_base(
      async_scope* scope,
      cleanup_t cleanup,
      inplace_stop_token stoken,
      bool detached) noexcept
    : scope_(scope),
      cleanup_(cleanup),
      // detached: refcount 1, detached bit true
      // attached: refcount 2, detached bit false
      refCount_{detached ? 3u : 4u},
      stoken_(std::move(stoken)) {}

  ~_spawn_op_base() {
    if (scope_) {
      stopCallback_.destruct();
    }
  }

  void abandon() noexcept {
    if (try_set_state(op_state::done)) {
      stopSource_.request_stop();
    }

    decref();
  }

  auto async_wait() noexcept {
    return evt_.async_wait();
  }

  inplace_stop_token get_stop_token() noexcept {
    return stopSource_.get_token();
  }

  void request_stop() noexcept {
    // first try to complete the receiver; we might be racing with the operation
    // so we need to use the atomic try_set_state to maintain consistency
    bool needsStop = try_set_state(op_state::done);

    // schedule the associated future to be woken up
    evt_.set();

    // don't bother requesting stop on the stop source unless try_set_state
    // succeeded; this should save some work if the operation was already
    // complete
    if (needsStop) {
      // do this after setting the event, above, so the future can potentially
      // complete while we run stop callbacks, etc.
      stopSource_.request_stop();
    }
  }

  void set_done() noexcept {
    if (try_set_state(op_state::done)) {
      evt_.set();
    }

    decref();
  }

  void start_failed() noexcept {
    scope_ = nullptr;
    set_done();
  }

 protected:
  async_scope* scope_;
  cleanup_t cleanup_;
  std::atomic<op_state> state_{op_state::incomplete};
  std::atomic<unsigned int> refCount_; // low bit stores detached state
  async_manual_reset_event evt_;
  inplace_stop_source stopSource_;
  inplace_stop_token stoken_;
  manual_lifetime<stop_callback_t> stopCallback_;

  bool detached() const noexcept {
    return refCount_.load(std::memory_order_relaxed) & 1u;
  }

  void decref() noexcept {
    auto oldRefCount = (refCount_.fetch_sub(2u, std::memory_order_acq_rel) >> 1);
    if (oldRefCount == 1u) {
      auto scope = scope_;
      cleanup_(this);
      if (scope) {
        record_done(scope);
      }
    }
  }

  void set_error(manual_lifetime<std::exception_ptr>& exception, std::exception_ptr e) noexcept {
    if (detached()) {
      std::terminate();
    }

    if (try_set_state(op_state::error)) {
      unifex::activate_union_member(exception, std::move(e));
      evt_.set();
    }

    decref();
  }

  void start() noexcept {
    stopCallback_.construct(std::move(stoken_), stop_callback{this});
  }

  bool try_set_state(op_state newState) noexcept {
    op_state expected = op_state::incomplete;
    if (!state_.compare_exchange_strong(
        expected, newState, std::memory_order_relaxed)) {
      return false;
    }

    return true;
  }
};

template <typename... Ts>
struct _spawn_op_promise final {
  struct type : _spawn_op_base {
    explicit type(
        async_scope* scope,
        cleanup_t cleanup,
        inplace_stop_token stoken,
        bool detached) noexcept
      : _spawn_op_base{scope, cleanup, std::move(stoken), detached} {}

    ~type() {
      auto state = state_.load(std::memory_order_relaxed);

      if (state == op_state::value) {
        unifex::deactivate_union_member(value_);
      }
      else if (state == op_state::error) {
        unifex::deactivate_union_member(exception_);
      }
    }

    template <typename... Values>
    void set_value(Values&&... values) noexcept {
      if (try_set_state(op_state::value)) {
        UNIFEX_TRY {
          unifex::activate_union_member(value_, (Values&&)values...);
        }
        UNIFEX_CATCH (...) {
          state_.store(op_state::error, std::memory_order_relaxed);
          unifex::activate_union_member(exception_, std::current_exception());
        }

        evt_.set();
      }

      decref();
    }

    void set_error(std::exception_ptr e) noexcept {
      _spawn_op_base::set_error(exception_, std::move(e));
    }

    template <typename Receiver>
    void consume(Receiver&& receiver) noexcept {
      switch (state_.load(std::memory_order_relaxed)) {
        case op_state::value:
          UNIFEX_TRY {
            std::apply([&](auto&&... values) {
              unifex::set_value((Receiver&&)receiver, std::move(values)...);
            }, std::move(value_).get());
          }
          UNIFEX_CATCH(...) {
            unifex::set_error((Receiver&&)receiver, std::current_exception());
          }
          break;

        case op_state::error:
          unifex::set_error((Receiver&&)receiver, std::move(exception_).get());
          break;

        case op_state::done:
          unifex::set_done((Receiver&&)receiver);
          break;

        default:
          std::terminate();
      }

      decref();
    }

    union {
      manual_lifetime<std::tuple<Ts...>> value_;
      manual_lifetime<std::exception_ptr> exception_;
    };
  };
};

template <typename Sender>
using spawn_op_promise_t = typename sender_value_types_t<Sender, single_overload, _spawn_op_promise>::type::type;

template <typename... Ts>
struct _ref_receiver final {
  struct type final {
    void set_value(Ts&&... values) noexcept {
      op_->set_value((Ts&&)values...);
    }

    void set_error(std::exception_ptr e) noexcept {
      op_->set_error(std::move(e));
    }

    void set_done() noexcept {
      op_->set_done();
    }

    friend inplace_stop_token
    tag_invoke(tag_t<get_stop_token>, const type& receiver) noexcept {
      return receiver.op_->get_stop_token();
    }

    typename _spawn_op_promise<Ts...>::type* op_;
  };
};

template <typename Sender>
using ref_receiver_t = typename sender_value_types_t<Sender, single_overload, _ref_receiver>::type::type;

template <typename Sender>
struct _spawn_op final {
  struct type final : spawn_op_promise_t<Sender> {
    explicit type(
        Sender&& sender,
        async_scope* scope,
        inplace_stop_token stoken,
        bool detached)
        noexcept(is_nothrow_connectable_v<Sender, ref_receiver_t<Sender>>)
      : spawn_op_promise_t<Sender>(
            scope, &cleanup, std::move(stoken), detached),
        op_(unifex::connect(
            std::move(sender), ref_receiver_t<Sender>{this})) {}

    explicit type(
        const Sender& sender,
        async_scope* scope,
        inplace_stop_token stoken,
        bool detached)
        noexcept(is_nothrow_connectable_v<const Sender&, ref_receiver_t<Sender>>)
      : spawn_op_promise_t<Sender>(scope, &cleanup, std::move(stoken), detached),
        op_(unifex::connect(
            sender, ref_receiver_t<Sender>{this})) {}

    void start() & noexcept {
      _spawn_op_base::start();
      unifex::start(op_);
    }

   private:
    static void cleanup(void* self) noexcept {
      delete static_cast<type*>(self);
    }

    using op_t = connect_result_t<Sender, ref_receiver_t<Sender>>;
    op_t op_;
  };
};

template <typename... Ts>
struct future final {
  using promise_t = typename _spawn_op_promise<Ts...>::type;

  struct promise_holder {
    explicit promise_holder(promise_t* p) noexcept
      : promise_(p) {}

    promise_holder(promise_holder&& other) noexcept
      : promise_(std::exchange(other.promise_, nullptr)) {}

    ~promise_holder() {
      if (promise_) {
        promise_->abandon();
      }
    }

    promise_holder& operator=(promise_holder rhs) noexcept {
      std::swap(promise_, rhs.promise_);
      return *this;
    }

    promise_t* promise_;
  };

  template <typename Receiver>
  struct _receiver final {
    struct type final : promise_holder {
      explicit type(promise_holder&& p, Receiver&& r)
          noexcept(is_nothrow_move_constructible_v<Receiver>)
        : promise_holder(std::move(p)),
          receiver_(std::move(r)) {
      }

      explicit type(promise_holder&& p, const Receiver& r)
          noexcept(is_nothrow_move_constructible_v<Receiver>)
        : promise_holder(std::move(p)),
          receiver_(r) {
      }

      void set_value() noexcept {
        auto p = std::exchange(this->promise_, nullptr);
        p->consume(std::move(receiver_));
      }

      void set_error(std::exception_ptr e) noexcept {
        unifex::set_error(std::move(receiver_), std::move(e));
      }

      void set_done() noexcept {
        unifex::set_done(std::move(receiver_));
      }

      template(typename CPO)
        (requires is_receiver_query_cpo_v<CPO>)
      friend auto tag_invoke(CPO&& cpo, const type& r) noexcept {
        return static_cast<CPO&&>(cpo)(r.receiver_);
      }

      Receiver receiver_;
    };
  };

  struct type final : promise_holder {
    template <template <class...> class Variant, template <class...> class Tuple>
    using value_types = Variant<Tuple<Ts...>>;

    template <template <class...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    auto connect(Receiver&& receiver) && noexcept {
      using future_receiver_t = typename _receiver<remove_cvref_t<Receiver>>::type;

      auto promise = this->promise_;
      return unifex::connect(
          let_value_with_stop_token([promise](auto stoken) noexcept {
            return let_value_with([promise, &stoken]() noexcept {
              auto stopCallback = [promise]() noexcept {
                promise->request_stop();
              };

              using stop_callback_t = typename inplace_stop_token::template callback_type<decltype(stopCallback)>;

              return stop_callback_t{std::move(stoken), std::move(stopCallback)};
            },
            [promise](auto&) noexcept {
              return promise->async_wait();
            });
          }),
          future_receiver_t{std::move(*this), (Receiver&&)receiver});
    }

   private:
    friend struct async_scope;

    explicit type(promise_t* p) noexcept
      : promise_holder(p) {}
  };
};

template <typename Sender>
using future_t = typename sender_value_types_t<Sender, single_overload, future>::type::type;

struct async_scope {
private:
  template <typename Scheduler, typename Sender>
  using _on_result_t =
    decltype(on(UNIFEX_DECLVAL(Scheduler&&), UNIFEX_DECLVAL(Sender&&)));

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

  template (typename Sender)
    (requires sender_to<Sender, ref_receiver_t<Sender>>)
  auto do_spawn(Sender&& sender, bool detached = false) {
    using spawn_op_t = typename _spawn_op<remove_cvref_t<Sender>>::type;

    // this could throw; if it does, there's nothing to clean up
    auto opToStart = std::make_unique<spawn_op_t>(
        (Sender&&)sender, this, stopSource_.get_token(), detached);

    // At this point, the rest of the function is noexcept.

    if (try_record_start()) {
      unifex::start(*opToStart);
    }
    else {
      opToStart->start_failed();
    }

    return opToStart;
  }

public:
  async_scope() noexcept = default;

  ~async_scope() {
    [[maybe_unused]] auto state = opState_.load(std::memory_order_relaxed);

    UNIFEX_ASSERT(is_stopping(state));
    UNIFEX_ASSERT(op_count(state) == 0);
  }

  template (typename Sender)
    (requires sender_to<Sender, ref_receiver_t<Sender>>)
  future_t<Sender> spawn(Sender&& sender) {
    return future_t<Sender>{do_spawn((Sender&&)sender).release()};
  }

  template (typename Sender, typename Scheduler)
    (requires scheduler<Scheduler> AND
     sender_to<
        _on_result_t<Scheduler, Sender>,
        ref_receiver_t<_on_result_t<Scheduler, Sender>>>)
  future_t<_on_result_t<Scheduler, Sender>> spawn_on(Scheduler&& scheduler, Sender&& sender) {
    return spawn(on((Scheduler&&) scheduler, (Sender&&) sender));
  }

  template (typename Scheduler, typename Fun)
    (requires scheduler<Scheduler> AND callable<Fun>)
  auto spawn_call_on(Scheduler&& scheduler, Fun&& fun) {
    return spawn_on(
      (Scheduler&&) scheduler,
      just_from((Fun&&) fun));
  }

  template (typename Sender)
    (requires sender_to<Sender, ref_receiver_t<Sender>>)
  void detached_spawn(Sender&& sender) {
    (void)do_spawn((Sender&&)sender, true /* detach */).release();
  }

  template (typename Sender, typename Scheduler)
    (requires scheduler<Scheduler> AND
     sender_to<
        _on_result_t<Scheduler, Sender>,
        ref_receiver_t<_on_result_t<Scheduler, Sender>>>)
  void detached_spawn_on(Scheduler&& scheduler, Sender&& sender) {
    detached_spawn(on((Scheduler&&) scheduler, (Sender&&) sender));
  }

  template (typename Scheduler, typename Fun)
    (requires scheduler<Scheduler> AND callable<Fun>)
  void detached_spawn_call_on(Scheduler&& scheduler, Fun&& fun) {
    detached_spawn_on(
      (Scheduler&&) scheduler,
      just_from((Fun&&) fun));
  }

  [[nodiscard]] auto complete() noexcept {
    return sequence(
        just_from([this] () noexcept {
          end_of_scope();
        }),
        await_and_sync());
  }

  [[nodiscard]] auto cleanup() noexcept {
    return sequence(
        just_from([this]() noexcept {
          request_stop();
        }),
        await_and_sync());
  }

  inplace_stop_token get_stop_token() noexcept {
    return stopSource_.get_token();
  }

  void request_stop() noexcept {
    end_of_scope();
    stopSource_.request_stop();
  }

 private:

  static constexpr std::size_t stoppedBit{1};

  static bool is_stopping(std::size_t state) noexcept {
    return (state & stoppedBit) == 0;
  }

  static std::size_t op_count(std::size_t state) noexcept {
    return state >> 1;
  }

  [[nodiscard]] bool try_record_start() noexcept {
    auto opState = opState_.load(std::memory_order_relaxed);

    do {
      if (is_stopping(opState)) {
        return false;
      }

      UNIFEX_ASSERT(opState + 2 > opState);
    } while (!opState_.compare_exchange_weak(
        opState,
        opState + 2,
        std::memory_order_relaxed));

    return true;
  }

  friend void record_done(async_scope* scope) noexcept {
    auto oldState = scope->opState_.fetch_sub(2, std::memory_order_release);

    if (is_stopping(oldState) && op_count(oldState) == 1) {
      // the scope is stopping and we're the last op to finish
      scope->evt_.set();
    }
  }

  void end_of_scope() noexcept {
    // stop adding work
    auto oldState = opState_.fetch_and(~stoppedBit, std::memory_order_release);

    if (op_count(oldState) == 0) {
      // there are no outstanding operations to wait for
      evt_.set();
    }
  }
};

} // namespace _async_scope

using _async_scope::async_scope;

template <typename... Ts>
using future = typename _async_scope::future<Ts...>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
