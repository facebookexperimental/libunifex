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

// A lot of stuff from cppcoro

#pragma once

#if !UNIFEX_NO_COROUTINES

#  include <unifex/any_scheduler.hpp>
#  include <unifex/async_scope.hpp>
#  include <unifex/await_transform.hpp>
#  include <unifex/create.hpp>
#  include <unifex/defer.hpp>
#  include <unifex/inline_scheduler.hpp>
#  include <unifex/just_void_or_done.hpp>
#  include <unifex/task.hpp>
#  include <unifex/with_scheduler_affinity.hpp>

#  include <optional>

#  include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename T>
class async_generator;

namespace detail {

// TODO: read_scheduler should be generalized to the read() sender factory from
// p2300, so we can do things like read(get_scheduler), read(get_stop_token),
// etc.
struct _read_scheduler_sender {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<any_scheduler>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = false;

  static constexpr auto blocking = blocking_kind::always_inline;

  template <class R>
  struct _op_state {
    R r_;

    friend void tag_invoke(tag_t<start>, _op_state& op) noexcept {
      try {
        set_value(std::move(op.r_), get_scheduler(op.r_));
      } catch (...) {
        set_error(std::move(op.r_), std::current_exception());
      }
    }
  };

  template <typename R>
  friend _op_state<std::decay_t<R>>
  tag_invoke(tag_t<connect>, _read_scheduler_sender, R&& r) {
    return {std::forward<R>(r)};
  }
};

struct _read_scheduler_t {
  constexpr _read_scheduler_sender operator()() const noexcept { return {}; }
};

inline constexpr _read_scheduler_t read_scheduler{};

template <typename Promise>
struct reschedule_receiver {
  std::optional<typename Promise::value_type> value_;
  unifex::coro::coroutine_handle<Promise> genCoro_;

  // set_value implies we are back on the correct scheduler => wake up the
  // receiver with the value from the generator
  void set_value() noexcept {
    if (value_) {
      unifex::set_value(std::move(*genCoro_.promise().receiverOpt_), *value_);
    } else {
      unifex::set_done(std::move(*genCoro_.promise().receiverOpt_));
    }
  }

  // set_done and set_error is just a forward into the receiver
  void set_done() noexcept {
    unifex::set_done(std::move(*genCoro_.promise().receiverOpt_));
  }

  template <typename E>
  void set_error(E&& err) noexcept {
    unifex::set_error(
        std::move(*genCoro_.promise().receiverOpt_), std::forward<E>(err));
  }
};

// Note this is also used in final_suspend, since it handles moving
// back to the consumer's scheduler, instead of using at_coroutine_exit
// as in unifex::task<>
template <typename T>
class async_generator_yield_operation {
public:
  using value_type = std::remove_reference_t<T>;

  async_generator_yield_operation(std::optional<value_type> value = {}) noexcept
    : value_{std::move(value)} {}

  bool await_ready() const noexcept { return false; }

  template <typename Promise>
  void await_suspend([[maybe_unused]] unifex::coro::coroutine_handle<Promise>
                         genCoro) noexcept {
    const auto& consumerSched = genCoro.promise().consumerSched_;
    if (unifex::get_scheduler(genCoro.promise()) != consumerSched) {
      genCoro.promise().rescheduleOpSt_ = unifex::connect(
          unifex::schedule(consumerSched),
          reschedule_receiver<Promise>{std::move(value_), genCoro});
      unifex::start(*genCoro.promise().rescheduleOpSt_);
      return;
    }

    if (value_) {
      unifex::set_value(std::move(*genCoro.promise().receiverOpt_), *value_);
    } else {
      unifex::set_done(std::move(*genCoro.promise().receiverOpt_));
    }
  }

  void await_resume() noexcept {}

private:
  std::optional<value_type> value_;
};

template <typename T>
class async_generator_promise {
public:
  using value_type = std::remove_reference_t<T>;
  using Promise = async_generator_promise<T>;

  async_generator_promise() noexcept : exception_(nullptr) {
    // Other variables left intentionally uninitialised as they're
    // only referenced in certain states by which time they should
    // have been initialised.
  }

  async_generator_promise(const async_generator_promise& other) = delete;
  async_generator_promise&
  operator=(const async_generator_promise& other) = delete;

  std::suspend_always initial_suspend() const noexcept { return {}; }

  void unhandled_exception() noexcept { exception_ = std::current_exception(); }

  void return_void() noexcept {}

  void rethrow_if_unhandled_exception() {
    if (exception_) {
      std::rethrow_exception(std::move(exception_));
    }
  }

  // This is the CP needed for other things in the ecosystem to be able to
  // "access" the current scheduler of our promise.
  friend unifex::any_scheduler tag_invoke(
      unifex::tag_t<unifex::get_scheduler>,
      const async_generator_promise& p) noexcept {
    return *p.sched_;
  }

  // This is needed for at_coroutine_exit to do the async clean up
  friend unifex::continuation_handle<> tag_invoke(
      const unifex::tag_t<unifex::exchange_continuation>&,
      async_generator_promise& p,
      unifex::continuation_handle<> action) noexcept {
    return std::exchange(p.continuation_, std::move(action));
  }

  unifex::coro::coroutine_handle<> unhandled_done() noexcept {
    unifex::set_done(std::move(*receiverOpt_));
    return coro::noop_coroutine();
  }

  async_generator<T> get_return_object() noexcept {
    return async_generator<T>{*this};
  }

  async_generator_yield_operation<T> final_suspend() noexcept { return {}; }

  async_generator_yield_operation<T> yield_value(value_type& value) noexcept {
    return async_generator_yield_operation<T>{value};
  }

  async_generator_yield_operation<T> yield_value(value_type&& value) noexcept {
    return yield_value(value);
  }

  template <typename Value>
  decltype(auto) await_transform(Value&& value) {
    // If the sender we're awaiting for is unifex::schedule
    if constexpr (unifex::is_sender_for_v<
                      unifex::remove_cvref_t<Value>,
                      unifex::schedule>) {
      return transform_schedule_sender_(static_cast<Value&&>(value));
    }
    // If we already have a sender => just await it
    else if constexpr (unifex::sender<Value>) {
      return unifex::await_transform(
          *this,
          unifex::with_scheduler_affinity(
              static_cast<Value&&>(value), *this->sched_));
    }
    // Either await_transform has been customized or Value is an awaitable.
    // Either way, we can dispatch to the await_transform CPO, then insert a
    // transition back to the correct execution context if necessary.
    else if constexpr (
        unifex::tag_invocable<
            unifex::tag_t<unifex::await_transform>,
            decltype(*this),
            Value> ||
        unifex::detail::_awaitable<Value>) {
      return unifex::with_scheduler_affinity(
          *this,
          unifex::await_transform(*this, static_cast<Value&&>(value)),
          *this->sched_);
    } else {
      // Otherwise, we don't know how to await this type. Just return it and
      // let the compiler issue a diagnostic.
      return (Value &&) value;
    }
  }

private:
  void transform_schedule_sender_impl_(unifex::any_scheduler newSched) {
    this->sched_ = std::move(newSched);
  }

  template <typename ScheduleSender>
  decltype(auto) transform_schedule_sender_(ScheduleSender&& snd) {
    // This sender is a scheduler provider. Get the scheduler. This
    // get_scheduler call returns a reference to the scheduler stored within
    // snd, which is an object whose lifetime spans a suspend point. So it's
    // ok to build an any_scheduler_ref from it:
    transform_schedule_sender_impl_(unifex::get_scheduler(snd));

    // Return the inner sender, appropriately wrapped in an awaitable:
    return unifex::await_transform(
        *this, std::forward<ScheduleSender>(snd).base());
  }

  // Friends with access to private fields
  friend class async_generator<T>;
  friend class async_generator_yield_operation<T>;
  friend class reschedule_receiver<Promise>;

  inline static constexpr unifex::inline_scheduler _default_scheduler{};

  std::optional<unifex::any_operation_state_for<reschedule_receiver<Promise>>>
      rescheduleOpSt_;
  std::optional<unifex::any_scheduler> sched_;
  // Keep track of the consumer scheduler
  unifex::any_scheduler consumerSched_{_default_scheduler};
  std::exception_ptr exception_;
  // In this case, this keeps the consumer coroutine + a done() continuation.
  // it's needed for at_coroutine exit for now, but also whenw e handle stop
  // requests.
  unifex::continuation_handle<> continuation_;
  std::optional<any_receiver_ref<T&>> receiverOpt_;
};

}  // namespace detail

template <typename T>
class [[nodiscard]] async_generator {
public:
  using promise_type = detail::async_generator_promise<T>;

  async_generator() noexcept : coroutine_(nullptr) {}

  explicit async_generator(promise_type& promise) noexcept
    : coroutine_(
          unifex::coro::coroutine_handle<promise_type>::from_promise(promise)) {
  }

  async_generator(async_generator&& other) noexcept
    : coroutine_(other.coroutine_) {
    other.coroutine_ = nullptr;
  }

  ~async_generator() {
    if (coroutine_) {
      coroutine_.destroy();
    }
  }

  async_generator& operator=(async_generator&& other) noexcept {
    async_generator temp(std::move(other));
    swap(temp);
    return *this;
  }

  async_generator(const async_generator&) = delete;
  async_generator& operator=(const async_generator&) = delete;

  // Potential problem: not sure if get_scheduler(gen.next()) would return
  // the right thing here. Perhaps we need a wrapper sender that also records
  // each next sender's scheduler and customizes get_scheduler(...)
  auto next() noexcept {
    // grab the receiver's scheduler; assume that the next-sender
    // is always started on the corresponding context
    return detail::read_scheduler() | let_value([this](auto sched) {
             if (!coroutine_.promise().sched_) {
               // capture the receiver's scheduler as the stream's
               // scheduler on the first run of the next-sender
               coroutine_.promise().sched_ = sched;
             }
             coroutine_.promise().consumerSched_ = sched;

             // check to see if we're currently running on the saved scheduler
             return just_void_or_done(coroutine_.promise().sched_ == sched)
                 // get back on the desired scheduler when we're not already
                 // there
                 | let_done([this]() {
                      return schedule(*coroutine_.promise().sched_);
                    }) |
                 let_value([this]() {
                      // once we're on the right scheduler, use create<>()
                      // to resume the generator's coroutine_handle<> after
                      // saving the create-receiver in the promise so we can
                      // complete create-sender from within the generator
                      return create<T>([this](auto& rec) {
                        any_receiver_ref<T&> r{inplace_stop_token{}, &rec};
                        coroutine_.promise().receiverOpt_ = r;
                        coroutine_.resume();
                      });
                    });
           });
  }

  auto cleanup() noexcept {
    return unifex::defer([this]() noexcept { return unifex::just_done(); });
  }

  void swap(async_generator& other) noexcept {
    using std::swap;
    swap(coroutine_, other.coroutine_);
  }

  unifex::coro::coroutine_handle<promise_type> coroutine_;
};

}  // namespace unifex

#endif

#include <unifex/detail/epilogue.hpp>
