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
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/then.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/on.hpp>

#include <atomic>
#include <memory>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _async_scope {

struct async_scope;

struct _receiver_base {
  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const _receiver_base& r) noexcept {
    return r.stopToken_;
  }

  inplace_stop_token stopToken_;
  void* op_;
  async_scope* scope_;
  void (*cleanup_)(void*) noexcept;
};

template <typename Result>
struct _receiver final {
  struct type;
};

template <typename Sender>
using result_t = typename sender_value_types_t<Sender, single_overload, std::tuple>::type;

template <typename Sender>
using receiver_t = typename _receiver<result_t<Sender>>::type;

void record_done(async_scope*) noexcept;

template <typename T>
struct promise {
  promise() {}

  ~promise() {
    if (state_ == state::value) {
      unifex::deactivate_union_member(value_);
    } else if (state_ == state::error) {
      unifex::deactivate_union_member(exception_);
    }
  }

  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    if (try_set_state(state::value)) {
      UNIFEX_TRY {
        unifex::activate_union_member(value_, (Values&&)values...);
      }
      UNIFEX_CATCH (...) {
        state_.store(state::error, std::memory_order_relaxed);
        unifex::activate_union_member(exception_, std::current_exception());
      }

      evt_.set();
    }
  }

  void set_error(std::exception_ptr e) noexcept {
    if (try_set_state(state::error)) {
      unifex::activate_union_member(exception_, std::move(e));
      evt_.set();
    }
  }

  void set_done() noexcept {
    if (try_set_state(state::done)) {
      evt_.set();
    }
  }

  template <typename Receiver>
  void consume(Receiver&& receiver) noexcept {
    switch (state_.load(std::memory_order_relaxed)) {
      case state::value:
        UNIFEX_TRY {
          std::apply([&](auto&&... values) {
            unifex::set_value((Receiver&&)receiver, std::move(values)...);
          }, std::move(value_).get());
        }
        UNIFEX_CATCH(...) {
          unifex::set_error((Receiver&&)receiver, std::current_exception());
        }
        break;

      case state::error:
        unifex::set_error((Receiver&&)receiver, std::move(exception_).get());
        break;

      case state::done:
        unifex::set_done((Receiver&&)receiver);
        break;

      default:
        std::terminate();
    }
  }

  void decref() noexcept {
    if (1 == refCount_.fetch_sub(1, std::memory_order_acq_rel)) {
      delete this;
    }
  }

  union {
    manual_lifetime<T> value_;
    manual_lifetime<std::exception_ptr> exception_;
  };

  enum class state : unsigned int { incomplete, done, value, error };
  std::atomic<state> state_ = state::incomplete;
  std::atomic<unsigned int> refCount_{2};
  async_manual_reset_event evt_;

  bool try_set_state(state newState) noexcept {
    state expected = state::incomplete;
    if (!state_.compare_exchange_strong(
        expected, newState, std::memory_order_relaxed)) {
      return false;
    }

    return true;
  }
};

template <typename Result>
struct _receiver<Result>::type final : _receiver_base {
  template <typename Op>
  explicit type(
      inplace_stop_token stoken,
      Op* op,
      async_scope* scope,
      std::unique_ptr<promise<Result>> p) noexcept
    : _receiver_base{stoken, op, scope, [](void* p) noexcept {
        auto* op = static_cast<Op*>(p);
        op->destruct();
        delete op;
      }},
      promise_{p.release()} {}

  type(type&& other) noexcept
    : _receiver_base(std::move(other)),
      promise_(std::exchange(other.promise_, nullptr)) {
  }

  ~type() {
    if (promise_) {
      promise_->decref();
    }
  }

  // it's just simpler to skip this
  type& operator=(type&&) = delete;

  void set_error(std::exception_ptr e) noexcept {
    promise_->set_error(std::move(e));
    complete();
  }

  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    promise_->set_value((Values&&)values...);
    complete();
  }

  void set_done() noexcept {
    promise_->set_done();
    complete();
  }

 private:
  void complete() noexcept {
    // we're about to delete this, so save the scope for later
    auto scope = scope_;
    cleanup_(op_);
    record_done(scope);
  }

  promise<Result>* promise_;
};

template <typename Sender>
using _operation_t = connect_result_t<Sender, receiver_t<Sender>>;

template <typename... Ts>
struct lazy final {
  struct promise_holder {
    explicit promise_holder(promise<std::tuple<Ts...>>* p) noexcept
      : promise_(p) {}

    promise_holder(promise_holder&& other) noexcept
      : promise_(std::exchange(other.promise_, nullptr)) {}

    ~promise_holder() {
      if (promise_) {
        promise_->set_done();
        promise_->decref();
      }
    }

    promise_holder& operator=(promise_holder rhs) noexcept {
      std::swap(promise_, rhs.promise_);
      return *this;
    }

    promise<std::tuple<Ts...>>* promise_;
  };

  template <typename Receiver>
  struct _receiver final : promise_holder {
    explicit _receiver(promise_holder&& p, Receiver&& r)
        noexcept(is_nothrow_move_constructible_v<Receiver>)
      : promise_holder(std::move(p)),
        receiver_(std::move(r)) {
    }

    explicit _receiver(promise_holder&& p, const Receiver& r)
        noexcept(is_nothrow_move_constructible_v<Receiver>)
      : promise_holder(std::move(p)),
        receiver_(r) {
    }

    void set_value() noexcept {
      auto p = std::exchange(this->promise_, nullptr);
      p->consume(std::move(receiver_));
      p->decref();
    }

    void set_error(std::exception_ptr e) noexcept {
      unifex::set_error(std::move(receiver_), std::move(e));
    }

    void set_done() noexcept {
      unifex::set_done(std::move(receiver_));
    }

    template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
    friend auto tag_invoke(CPO&& cpo, const _receiver& r) noexcept {
      return static_cast<CPO&&>(cpo)(r.receiver_);
    }

    Receiver receiver_;
  };

  struct type final : promise_holder {
    template <template <class...> class Variant, template <class...> class Tuple>
    using value_types = Variant<Tuple<Ts...>>;

    template <template <class...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    auto connect(Receiver&& receiver) && noexcept {
      using receiver_t = _receiver<remove_cvref_t<Receiver>>;

      auto& evt = this->promise_->evt_;
      return unifex::connect(
          evt.async_wait(), receiver_t{std::move(*this), (Receiver&&)receiver});
    }

   private:
    friend struct async_scope;

    explicit type(promise<std::tuple<Ts...>>* p) noexcept
      : promise_holder(p) {}
  };
};

template <typename Sender>
using lazy_t = typename sender_value_types_t<Sender, single_overload, lazy>::type::type;

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

public:
  async_scope() noexcept = default;

  ~async_scope() {
    [[maybe_unused]] auto state = opState_.load(std::memory_order_relaxed);

    UNIFEX_ASSERT(is_stopping(state));
    UNIFEX_ASSERT(op_count(state) == 0);
  }

  template (typename Sender)
    (requires sender_to<Sender, receiver_t<Sender>>)
  lazy_t<Sender> spawn(Sender&& sender) {
    // these allocations could throw; if either does, there's nothing to clean up
    auto opToStart = std::make_unique<manual_lifetime<_operation_t<Sender>>>();

    auto p = std::make_unique<promise<result_t<Sender>>>();

    // the construction of these locals is noexcept
    lazy_t<Sender> ret{p.get()};
    receiver_t<Sender> rcvr{
        stopSource_.get_token(),
        opToStart.get(),
        this,
        std::move(p)};

    // this could throw; if it does, the only clean-up we need is to
    // deallocate the manual_lifetime, which is handled by opToStart's
    // destructor so we're good
    opToStart->construct_with([&] {
      return connect((Sender&&) sender, std::move(rcvr));
    });

    // At this point, the rest of the function is noexcept, but opToStart's
    // destructor is no longer enough to properly clean up because it won't
    // invoke destruct().  We need to ensure that we either call destruct()
    // ourselves or complete the operation so *it* can call destruct().

    if (try_record_start()) {
      // start is noexcept so we can assume that the operation will complete
      // after this, which means we can rely on its self-ownership to ensure
      // that it is eventually deleted
      unifex::start(opToStart.release()->get());
    }
    else {
      // we've been stopped so clean up and bail out
      opToStart->destruct();
    }

    return ret;
  }

  template (typename Sender, typename Scheduler)
    (requires scheduler<Scheduler> AND
     sender_to<
        _on_result_t<Scheduler, Sender>,
        receiver_t<_on_result_t<Scheduler, Sender>>>)
  lazy_t<_on_result_t<Scheduler, Sender>> spawn_on(Scheduler&& scheduler, Sender&& sender) {
    return spawn(on((Scheduler&&) scheduler, (Sender&&) sender));
  }

  template (typename Scheduler, typename Fun)
    (requires scheduler<Scheduler> AND callable<Fun>)
  auto spawn_call_on(Scheduler&& scheduler, Fun&& fun) {
    return spawn_on(
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
using lazy = typename _async_scope::lazy<Ts...>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
