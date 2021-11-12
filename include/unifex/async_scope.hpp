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

inplace_stop_token get_stop_token_from_scope(async_scope*) noexcept;

enum class op_state : unsigned int {
  incomplete,
  done,
  value,
  error,
  abandoned,
  detached
};

struct _spawn_op_base {
  struct stop_callback final {
    _spawn_op_base* self;

    void operator()() noexcept { self->request_stop(); }
  };

  using stop_callback_t =
      typename inplace_stop_token::template callback_type<stop_callback>;

  using cleanup_t = void (*)(void*) noexcept;

  explicit _spawn_op_base(
      async_scope* scope, cleanup_t cleanup, bool detached) noexcept
    : scope_(scope)
    , cleanup_(cleanup)
    , state_{detached ? op_state::detached : op_state::incomplete}
    , refCount_{detached ? 1u : 2u} {}

  ~_spawn_op_base() {
    // if scope_ is nullptr here then the operation failed to start, which means
    // we didn't construct the stop callback and thus must not destruct it.
    if (scope_) {
      stopCallback_.destruct();
    }
  }

  /**
   * Invoked by the attached future<> when it goes out of scope without having
   * been connected and started.
   */
  void abandon() noexcept {
    if (try_set_state(op_state::abandoned)) {
      // if we succeeded in marking the promise as abandoned then the upstream
      // operation is still running; request that it stop soon.
      stopSource_.request_stop();

      // we don't bother setting the event here because no one's going to
      // connect to it
    }

    decref();
  }

  auto async_wait() noexcept { return evt_.async_wait(); }

  inplace_stop_token get_stop_token() noexcept {
    return stopSource_.get_token();
  }

  /**
   * Invoked by stop callbacks to request that this operation stop.
   *
   * This method is called in two scenarios:
   *  - the attached future<> is cancelled, or
   *  - the associated async_scope is cancelled.
   */
  void request_stop() noexcept {
    // fulfill the request by requesting stop on the operation's stop source
    stopSource_.request_stop();

    // now, try to complete the attached future<> promptly; this will allow the
    // future<> to complete in parallel with an operation that's slow to cancel
    if (try_set_state(op_state::done)) {
      evt_.set();
    }

    // don't call decref() here; the caller is not an owning reference
  }

  /**
   * Invoked by unifex::set_done when the operation completes with done.
   *
   * Wakes up the attached future<> if it's still there.
   */
  void set_done() noexcept {
    // only bother to notify the waiting future<> if it's still there
    if (try_set_state(op_state::done)) {
      evt_.set();
    }

    decref();
  }

  /**
   * Invoked when the attempt to spawn this operation fails because the
   * associated async_scope has already ended.  Puts this operation in the done
   * and signalled state so that the attached future<> can complete immediately
   * with done.
   */
  void start_failed() noexcept {
    // mark the operation as not started
    scope_ = nullptr;

    // ensure an attached future<> is immediately ready
    set_done();
  }

protected:
  // the async_scope that spawned this operation; set to null if the spawn fails
  async_scope* scope_;
  // a type-erased deleter to clean up this operation when its refcount hits 0
  cleanup_t cleanup_;
  // where this operation is in its lifecycle
  std::atomic<op_state> state_;
  // this operation's reference count; starts at two if there's an attached
  // future<>, otherwise starts at one.  The operation deletes itself when its
  // refcount hits zero.
  std::atomic<unsigned int> refCount_;
  // an event that starts unset and becomes set when the operation completes
  async_manual_reset_event evt_;
  // a stop source for the spawned operation that we can use to cancel it from
  // either the associated scope or the attached future<>
  inplace_stop_source stopSource_;
  // a stop callback that listens for stop requests from the associated scope
  manual_lifetime<stop_callback_t> stopCallback_;

  bool detached() const noexcept {
    return op_state::detached == state_.load(std::memory_order_relaxed);
  }

  /**
   * Decrements the refcount and cleans up if the caller is the last owner.
   *
   * Cleaning up means destroying the operations state and then notifying the
   * associated scope (if there is one) that the operation has completed.
   */
  void decref() noexcept {
    if (1u == refCount_.fetch_sub(1u, std::memory_order_acq_rel)) {
      // save the scope in a local because we're about to delete this
      auto scope = scope_;

      // destroy the operation state
      cleanup_(this);

      if (scope) {
        record_done(scope);
      }
    }
  }

  /**
   * Invoked when the operation completes with an error.
   *
   * The consequences depend on the observed state:
   *  - if there's a waiting, attached future<>, it'll be woken up to complete
   *    with the given exception;
   *  - if the attached future<> abandoned this operation's result, the error is
   *    ignored; otherwise
   *  - if the operation is detached, std::terminate() is invoked.
   */
  void set_error(
      manual_lifetime<std::exception_ptr>& exception,
      std::exception_ptr e) noexcept {
    if (try_set_state(op_state::error)) {
      unifex::activate_union_member(exception, std::move(e));
      evt_.set();
    } else if (detached()) {
      std::terminate();
    }

    decref();
  }

  void start() noexcept {
    stopCallback_.construct(
        get_stop_token_from_scope(scope_), stop_callback{this});
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
    explicit type(async_scope* scope, cleanup_t cleanup, bool detached) noexcept
      : _spawn_op_base{scope, cleanup, detached} {}

    ~type() {
      // since we're in the destructor, we've called decref() since storing
      // either a value or an exception; therefore, the state of the value or
      // exception has synchronized because of the memory order of the fetch_sub
      // in decref(), which makes it safe to inspect the state with a relaxed
      // load here.
      auto state = state_.load(std::memory_order_relaxed);

      if (state == op_state::value) {
        unifex::deactivate_union_member(value_);
      } else if (state == op_state::error) {
        unifex::deactivate_union_member(exception_);
      }
    }

    /**
     * Invoked when the operation completes with a value.
     *
     * Wakes up the attached future<> if it's still there.
     */
    template(typename... Values)                                       //
        (requires constructible_from<std::tuple<Ts...>, Values&&...>)  //
        void set_value(Values&&... values) noexcept {
      if (try_set_state(op_state::value)) {
        UNIFEX_TRY {
          unifex::activate_union_member(value_, (Values &&) values...);
        }
        UNIFEX_CATCH(...) {
          state_.store(op_state::error, std::memory_order_relaxed);
          unifex::activate_union_member(exception_, std::current_exception());
        }

        evt_.set();
      }

      decref();
    }

    /**
     * Invoked when the operation completes with an error; terminates if the
     * operation is detached.
     *
     * Wakes up the attached future<> if it's still there.
     */
    void set_error(std::exception_ptr e) noexcept {
      _spawn_op_base::set_error(exception_, std::move(e));
    }

    /**
     * Consumes the result of this operation by completing the given receiver.
     */
    template(typename Receiver)                    //
        (requires receiver_of<Receiver, Ts&&...>)  //
        void consume(Receiver&& receiver) noexcept {
      // we're being invoked by the attached future<>, which means we've
      // synchronized with it by virtue of setting our event so this relaxed
      // load is safe
      switch (state_.load(std::memory_order_relaxed)) {
        case op_state::value:
          UNIFEX_TRY {
            std::apply(
                [&](auto&&... values) {
                  unifex::set_value(
                      (Receiver &&) receiver,
                      static_cast<decltype(values)>(values)...);
                },
                std::move(value_).get());
          }
          UNIFEX_CATCH(...) {
            unifex::set_error((Receiver &&) receiver, std::current_exception());
          }
          break;

        case op_state::error:
          unifex::set_error(
              (Receiver &&) receiver, std::move(exception_).get());
          break;

        case op_state::done: unifex::set_done((Receiver &&) receiver); break;

        default:
          // the other possible states are incomplete, abandoned, and detached,
          // none of which should be observable here
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
using spawn_op_promise_t =
    typename sender_value_types_t<Sender, single_overload, _spawn_op_promise>::
        type::type;

/**
 * A receiver that delegates all operations to a spawn_op_promise.
 */
template <typename... Ts>
struct _delegate_receiver final {
  struct type final {
    template(typename... Values)                                       //
        (requires constructible_from<std::tuple<Ts...>, Values&&...>)  //
        void set_value(Values&&... values) noexcept {
      op_->set_value((Values &&) values...);
    }

    void set_error(std::exception_ptr e) noexcept {
      op_->set_error(std::move(e));
    }

    void set_done() noexcept { op_->set_done(); }

    friend inplace_stop_token
    tag_invoke(tag_t<get_stop_token>, const type& receiver) noexcept {
      return receiver.op_->get_stop_token();
    }

    typename _spawn_op_promise<Ts...>::type* op_;
  };
};

template <typename Sender>
using delegate_receiver_t =
    typename sender_value_types_t<Sender, single_overload, _delegate_receiver>::
        type::type;

template <typename Sender>
struct _spawn_op final {
  struct type final : spawn_op_promise_t<Sender> {
    explicit type(Sender&& sender, async_scope* scope, bool detached) noexcept(
        is_nothrow_connectable_v<Sender, delegate_receiver_t<Sender>>)
      : spawn_op_promise_t<Sender>(scope, &cleanup, detached)
      , op_(unifex::connect(
            std::move(sender), delegate_receiver_t<Sender>{this})) {}

    explicit type(const Sender& sender, async_scope* scope, bool detached) noexcept(
        is_nothrow_connectable_v<const Sender&, delegate_receiver_t<Sender>>)
      : spawn_op_promise_t<Sender>(scope, &cleanup, detached)
      , op_(unifex::connect(sender, delegate_receiver_t<Sender>{this})) {}

    void start() & noexcept {
      _spawn_op_base::start();
      unifex::start(op_);
    }

  private:
    static void cleanup(void* self) noexcept {
      delete static_cast<type*>(self);
    }

    using op_t = connect_result_t<Sender, delegate_receiver_t<Sender>>;
    op_t op_;
  };
};

template <typename... Ts>
struct future final {
  using promise_t = typename _spawn_op_promise<Ts...>::type;

  /**
   * An owning handle to a spawn_op_promise.
   *
   * Abandons the promise on destruction if ownership hasn't been transferred
   * elsewhere first.
   */
  struct promise_handle {
    explicit promise_handle(promise_t* p) noexcept : promise_(p) {}

    promise_handle(promise_handle&& other) noexcept
      : promise_(std::exchange(other.promise_, nullptr)) {}

    ~promise_handle() {
      if (promise_) {
        promise_->abandon();
      }
    }

    promise_handle& operator=(promise_handle rhs) noexcept {
      std::swap(promise_, rhs.promise_);
      return *this;
    }

    promise_t* promise_;
  };

  /**
   * A Receiver that future<> connects to a promise's async_manual_reset_event
   * to be notified when the promise is complete.
   */
  template <typename Receiver>
  struct _receiver final {
    struct type final : promise_handle {
      explicit type(promise_handle&& p, Receiver&& r) noexcept(
          is_nothrow_move_constructible_v<Receiver>)
        : promise_handle(std::move(p))
        , receiver_(std::move(r)) {}

      explicit type(promise_handle&& p, const Receiver& r) noexcept(
          is_nothrow_copy_constructible_v<Receiver>)
        : promise_handle(std::move(p))
        , receiver_(r) {}

      void set_value() noexcept {
        auto p = std::exchange(this->promise_, nullptr);
        p->consume(std::move(receiver_));
      }

      void set_error(std::exception_ptr e) noexcept {
        unifex::set_error(std::move(receiver_), std::move(e));
      }

      void set_done() noexcept { unifex::set_done(std::move(receiver_)); }

      template(typename CPO)                       //
          (requires is_receiver_query_cpo_v<CPO>)  //
          friend auto tag_invoke(CPO&& cpo, const type& r) noexcept {
        return static_cast<CPO&&>(cpo)(r.receiver_);
      }

      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    };
  };

  /**
   * A future<T...>::type is a handle to an eagerly-started operation; it is
   * also a Sender so you retrieve the result by connecting it to an appropriate
   * Receiver and starting the resulting Operation.
   *
   * Discarding a future without connecting or starting it requests cancellation
   * of the associated operation and discards the operation's result when it
   * ultimately completes.
   *
   * Requesting cancellation of a connected-and-started future will also request
   * cancellation of the associated operation.
   */
  struct type final : promise_handle {
    template <
        template <class...>
        class Variant,
        template <class...>
        class Tuple>
    using value_types = Variant<Tuple<Ts...>>;

    template <template <class...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template(typename Receiver)  //
        (requires receiver_of<Receiver, Ts...> AND
             scheduler_provider<Receiver>)  //
        auto connect(Receiver&& receiver) && noexcept {
      using future_receiver_t =
          typename _receiver<remove_cvref_t<Receiver>>::type;

      auto promise = this->promise_;
      return unifex::connect(
          let_value_with_stop_token([promise](auto stoken) noexcept {
            return let_value_with(
                [promise, stoken = std::move(stoken)]() mutable noexcept {
                  return _spawn_op_base::stop_callback_t{
                      std::move(stoken),
                      _spawn_op_base::stop_callback{promise}};
                },
                [promise](auto&) noexcept { return promise->async_wait(); });
          }),
          future_receiver_t{std::move(*this), (Receiver &&) receiver});
    }

  private:
    friend struct async_scope;

    explicit type(promise_t* p) noexcept : promise_handle(p) {}
  };
};

template <typename Sender>
using future_t =
    typename sender_value_types_t<Sender, single_overload, future>::type::type;

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

  template(typename Sender)                                      //
      (requires sender_to<Sender, delegate_receiver_t<Sender>>)  //
      auto do_spawn(Sender&& sender, bool detached = false) {
    using spawn_op_t = typename _spawn_op<remove_cvref_t<Sender>>::type;

    // this could throw; if it does, there's nothing to clean up
    auto opToStart =
        std::make_unique<spawn_op_t>((Sender &&) sender, this, detached);

    // At this point, the rest of the function is noexcept.

    if (try_record_start()) {
      unifex::start(*opToStart);
    } else {
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

  /**
   * Connects and starts the given Sender, returning a future<> with which you
   * can observe the result.
   */
  template(typename Sender)                                      //
      (requires sender_to<Sender, delegate_receiver_t<Sender>>)  //
      future_t<Sender> spawn(Sender&& sender) {
    return future_t<Sender>{do_spawn((Sender &&) sender).release()};
  }

  /**
   * Equivalent to spawn(on((Scheduler&&) scheduler, (Sender&&)sender)).
   */
  template(typename Sender, typename Scheduler)  //
      (requires scheduler<Scheduler> AND         //
           sender_to<
               _on_result_t<Scheduler, Sender>,
               delegate_receiver_t<_on_result_t<Scheduler, Sender>>>)  //
      future_t<_on_result_t<Scheduler, Sender>> spawn_on(
          Scheduler&& scheduler, Sender&& sender) {
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
  template(typename Sender)                                      //
      (requires sender_to<Sender, delegate_receiver_t<Sender>>)  //
      void detached_spawn(Sender&& sender) {
    (void)do_spawn((Sender &&) sender, true /* detach */).release();
  }

  /**
   * Equivalent to detached_spawn(on((Scheduler&&) scheduler,
   * (Sender&&)sender)).
   */
  template(typename Sender, typename Scheduler)  //
      (requires scheduler<Scheduler> AND         //
           sender_to<
               _on_result_t<Scheduler, Sender>,
               delegate_receiver_t<_on_result_t<Scheduler, Sender>>>)  //
      void detached_spawn_on(Scheduler&& scheduler, Sender&& sender) {
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
  [[nodiscard]] bool try_record_start() noexcept {
    auto opState = opState_.load(std::memory_order_relaxed);

    do {
      if (is_stopping(opState)) {
        return false;
      }

      UNIFEX_ASSERT(opState + 2 > opState);
    } while (!opState_.compare_exchange_weak(
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
};

}  // namespace _async_scope

using _async_scope::async_scope;

template <typename... Ts>
using future = typename _async_scope::future<Ts...>::type;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
