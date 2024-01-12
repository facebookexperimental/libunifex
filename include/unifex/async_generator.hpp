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
#  include <unifex/at_coroutine_exit.hpp>
#  include <unifex/await_transform.hpp>
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
template <typename T>
class async_generator_iterator;
class async_generator_yield_operation;
class async_generator_advance_operation;

class async_generator_promise_base {
public:
  async_generator_promise_base() noexcept : m_exception(nullptr) {
    // Other variables left intentionally uninitialised as they're
    // only referenced in certain states by which time they should
    // have been initialised.
  }

  async_generator_promise_base(const async_generator_promise_base& other) =
      delete;
  async_generator_promise_base&
  operator=(const async_generator_promise_base& other) = delete;

  std::suspend_always initial_suspend() const noexcept { return {}; }

  async_generator_yield_operation final_suspend() noexcept;

  void unhandled_exception() noexcept {
    m_exception = std::current_exception();
  }

  void return_void() noexcept {}

  /// Query if the generator has reached the end of the sequence.
  ///
  /// Only valid to call after resuming from an awaited advance operation.
  /// i.e. Either a begin() or iterator::operator++() operation.
  bool finished() const noexcept { return m_currentValue == nullptr; }

  void rethrow_if_unhandled_exception() {
    if (m_exception) {
      std::rethrow_exception(std::move(m_exception));
    }
  }

  // This is the CP needed for other things in the ecosystem to be able to
  // "access" the current scheduler of our promise.
  friend unifex::any_scheduler tag_invoke(
      unifex::tag_t<unifex::get_scheduler>,
      const async_generator_promise_base& p) noexcept {
    return p.sched_;
  }

  // This is needed for at_coroutine_exit to do the async clean up
  friend unifex::continuation_handle<> tag_invoke(
      const unifex::tag_t<unifex::exchange_continuation>&,
      async_generator_promise_base& p,
      unifex::continuation_handle<> action) noexcept {
    return std::exchange(p.continuation_, std::move(action));
  }

  unifex::coro::coroutine_handle<> unhandled_done() noexcept {
    return continuation_.done();
  }

protected:
  template <typename P>
  friend struct async_gen_initial_suspend;

  async_generator_yield_operation internal_yield_value() noexcept;

  // Needed for jumping back on the generator's scheduler, in cases
  // where the consumer coroutine is executing elsewhere.
  unifex::async_scope scope_;

  // Keep track of the consumer scheduler
  unifex::any_scheduler consumerSched_{_default_scheduler};
  // The scheduler we currently run on
  unifex::any_scheduler sched_{_default_scheduler};
  bool rescheduledBefore_{false};

private:
  friend class async_generator_yield_operation;
  friend class async_generator_advance_operation;

  inline static constexpr unifex::inline_scheduler _default_scheduler{};

  std::exception_ptr m_exception;

  // In this case, this keeps the consumer coroutine + a done() continuation.
  // it's needed for at_coroutine exit for now, but also whenw e handle stop
  // requests.
  unifex::continuation_handle<> continuation_;

protected:
  void* m_currentValue;
};

class async_generator_yield_operation final {
public:
  async_generator_yield_operation(
      unifex::continuation_handle<> continuation) noexcept
    : continuation_(continuation) {}

  bool await_ready() const noexcept { return false; }

  template <typename Promise>
  unifex::coro::coroutine_handle<>
  await_suspend([[maybe_unused]] unifex::coro::coroutine_handle<Promise>
                    producer) noexcept {
    // simplest case => no need to reschedule at all, just resume the cosumer
    // coroutine
    if (producer.promise().sched_ == producer.promise().consumerSched_) {
      return continuation_.handle();
    }

    // need to reschedule back onto the consumer coro; kick off an async event &
    // return no-op
    producer.promise().scope_.detached_spawn_call_on(
        producer.promise().consumerSched_,
        [consumerCoro = continuation_.handle()]() noexcept {
          consumerCoro.resume();
        });
    return unifex::coro::noop_coroutine();
  }

  void await_resume() noexcept {}

private:
  unifex::continuation_handle<> continuation_;
};

// await_suspend when we yield from the generator
inline async_generator_yield_operation
async_generator_promise_base::final_suspend() noexcept {
  // The same is done for unifex::task (check the cpp). This was confusing to
  // read, but all we're doing is at the very last suspend, we want to clear up
  // the async scope and schedule back onto the consumer's schedule.
  auto cleanupTask = unifex::at_coroutine_exit([this]() -> unifex::task<void> {
    co_await scope_.complete();

    if (consumerSched_ != sched_) {
      co_await unifex::schedule(consumerSched_);
    }
  });

  cleanupTask.await_suspend_impl_(*this);
  (void)cleanupTask.await_resume();

  m_currentValue = nullptr;
  return internal_yield_value();
}

inline async_generator_yield_operation
async_generator_promise_base::internal_yield_value() noexcept {
  return async_generator_yield_operation{continuation_};
}

class async_generator_advance_operation {
protected:
  async_generator_advance_operation(std::nullptr_t) noexcept
    : m_promise(nullptr)
    , m_producerCoroutine(nullptr) {}

  async_generator_advance_operation(
      async_generator_promise_base& promise,
      unifex::coro::coroutine_handle<> producerCoroutine) noexcept
    : m_promise(std::addressof(promise))
    , m_producerCoroutine(producerCoroutine) {}

public:
  bool await_ready() const noexcept { return false; }

  // await_suspend during co_await ++itr;
  template <typename Promise>
  unifex::coro::coroutine_handle<> await_suspend(
      unifex::coro::coroutine_handle<Promise> consumerCoroutine) noexcept {
    m_promise->continuation_ = consumerCoroutine;

    auto consumerScheduler = unifex::get_scheduler(consumerCoroutine.promise());

    // simplest case => no need to reschedule at all, just resume the producer
    // coroutine
    if (consumerScheduler == m_promise->sched_) {
      m_promise->consumerSched_ = consumerScheduler;
      return m_producerCoroutine;
    }

    // consumerScheduler != producerScheduler and the generator hasn't been
    // rescheduled
    // => continue executing on the consumer's schedule
    if (!m_promise->rescheduledBefore_) {
      m_promise->consumerSched_ = consumerScheduler;
      m_promise->sched_ = consumerScheduler;
      return m_producerCoroutine;
    }

    // consumerScheduler != producerScheduler and the generator has been
    // rescheduled => we need to resume onto the generator's scheduler; return
    // no-op + kick off an async event to hop onto the generator's scheduler
    m_promise->scope_.detached_spawn_call_on(
        m_promise->sched_,
        [prodCoro = unifex::coro::coroutine_handle<
             async_generator_promise_base>::from_promise(*m_promise),
         consumerSched = std::move(consumerScheduler)]() noexcept {
          // update consumerSched_ so we can re-hop onto the correct scheduler
          prodCoro.promise().consumerSched_ = consumerSched;
          // prodCoro.promise().sched_ already points to the correct scheduler
          prodCoro.resume();
        });
    return unifex::coro::noop_coroutine();
  }

protected:
  async_generator_promise_base* m_promise;
  unifex::coro::coroutine_handle<> m_producerCoroutine;
};

template <typename T>
class async_generator_promise final : public async_generator_promise_base {
  using value_type = std::remove_reference_t<T>;
  using Promise = async_generator_promise<T>;

public:
  async_generator_promise() noexcept = default;

  async_generator<T> get_return_object() noexcept;

  async_generator_yield_operation yield_value(value_type& value) noexcept {
    m_currentValue = std::addressof(value);
    return internal_yield_value();
  }

  async_generator_yield_operation yield_value(value_type&& value) noexcept {
    return yield_value(value);
  }

  T& value() const noexcept { return *static_cast<T*>(m_currentValue); }

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
              static_cast<Value&&>(value), this->sched_));
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
          this->sched_);
    } else {
      // Otherwise, we don't know how to await this type. Just return it and
      // let the compiler issue a diagnostic.
      return (Value &&) value;
    }
  }

  void transform_schedule_sender_impl_(unifex::any_scheduler newSched) {
    // this->consumerSched_ points to the correct scheduler
    this->rescheduledBefore_ = true;
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
};

template <typename T>
class async_generator_increment_operation final
  : public async_generator_advance_operation {
public:
  async_generator_increment_operation(
      async_generator_iterator<T>& iterator) noexcept
    : async_generator_advance_operation(
          iterator.m_coroutine.promise(), iterator.m_coroutine)
    , m_iterator(iterator) {}

  async_generator_iterator<T>& await_resume();

private:
  async_generator_iterator<T>& m_iterator;
};

template <typename T>
class async_generator_iterator final {
  using promise_type = async_generator_promise<T>;
  using handle_type = unifex::coro::coroutine_handle<promise_type>;

public:
  using iterator_category = std::input_iterator_tag;
  // Not sure what type should be used for difference_type as we don't
  // allow calculating difference between two iterators.
  using difference_type = std::ptrdiff_t;
  using value_type = std::remove_reference_t<T>;
  using reference = std::add_lvalue_reference_t<T>;
  using pointer = std::add_pointer_t<value_type>;

  async_generator_iterator(std::nullptr_t) noexcept : m_coroutine(nullptr) {}

  async_generator_iterator(handle_type coroutine) noexcept
    : m_coroutine(coroutine) {}

  async_generator_increment_operation<T> operator++() noexcept {
    return async_generator_increment_operation<T>{*this};
  }

  reference operator*() const noexcept { return m_coroutine.promise().value(); }

  bool operator==(const async_generator_iterator& other) const noexcept {
    return m_coroutine == other.m_coroutine;
  }

  bool operator!=(const async_generator_iterator& other) const noexcept {
    return !(*this == other);
  }

private:
  friend class async_generator_increment_operation<T>;

  handle_type m_coroutine;
};

template <typename T>
async_generator_iterator<T>&
async_generator_increment_operation<T>::await_resume() {
  if (m_promise->finished()) {
    // Update iterator to end()
    m_iterator = async_generator_iterator<T>{nullptr};
    m_promise->rethrow_if_unhandled_exception();
  }

  return m_iterator;
}

template <typename T>
class async_generator_begin_operation final
  : public async_generator_advance_operation {
  using promise_type = async_generator_promise<T>;
  using handle_type = unifex::coro::coroutine_handle<promise_type>;

public:
  async_generator_begin_operation(std::nullptr_t) noexcept
    : async_generator_advance_operation(nullptr) {}

  async_generator_begin_operation(handle_type producerCoroutine) noexcept
    : async_generator_advance_operation(
          producerCoroutine.promise(), producerCoroutine) {}

  bool await_ready() const noexcept {
    return m_promise == nullptr ||
        async_generator_advance_operation::await_ready();
  }

  async_generator_iterator<T> await_resume() {
    if (m_promise == nullptr) {
      // Called begin() on the empty generator.
      return async_generator_iterator<T>{nullptr};
    } else if (m_promise->finished()) {
      // Completed without yielding any values.
      m_promise->rethrow_if_unhandled_exception();
      return async_generator_iterator<T>{nullptr};
    }

    return async_generator_iterator<T>{
        handle_type::from_promise(*static_cast<promise_type*>(m_promise))};
  }
};
}  // namespace detail

template <typename T>
class [[nodiscard]] async_generator {
public:
  using promise_type = detail::async_generator_promise<T>;
  using iterator = detail::async_generator_iterator<T>;

  async_generator() noexcept : m_coroutine(nullptr) {}

  explicit async_generator(promise_type& promise) noexcept
    : m_coroutine(
          unifex::coro::coroutine_handle<promise_type>::from_promise(promise)) {
  }

  async_generator(async_generator&& other) noexcept
    : m_coroutine(other.m_coroutine) {
    other.m_coroutine = nullptr;
  }

  ~async_generator() {
    if (m_coroutine) {
      m_coroutine.destroy();
    }
  }

  async_generator& operator=(async_generator&& other) noexcept {
    async_generator temp(std::move(other));
    swap(temp);
    return *this;
  }

  async_generator(const async_generator&) = delete;
  async_generator& operator=(const async_generator&) = delete;

  auto next() noexcept {
    // defer checking whether it_ has been initialized until next() is
    // awaited it's not entirely clear whether this is necessary
    return unifex::defer([this]() noexcept {
      // just_void_or_done() tends to be a bit cheaper than a
      // variant_sender so map "has it_ been initialized?" into the done
      // and value channels so we can evaluate the equivalent of
      // it_ = co_await this->begin() only once
      return unifex::just_void_or_done(it_.has_value()) |
          // this let_value runs when it_.has_value() is true so
          // increment the iterator and return the new iterator value
          unifex::let_value(
                 [this]() noexcept { return unifex::as_sender(++*it_); }) |
          // this let_done runs when it_.has_value() is false so
          // initialize it_ to the result of awaiting begin() an then
          // return the result
          unifex::let_done([this]() noexcept {
               return unifex::as_sender(this->begin()) |
                   unifex::then([this](auto it) noexcept {
                        it_ = it;
                        return *it_;
                      });
             }) |
          // given the recently-incremented iterator as an argument,
          // translate the state of that iterator into either the value
          // it points to or a done signal
          unifex::let_value([this](auto it) noexcept {
               // we want a done signal if it points past the end of the
               // range
               return unifex::just_void_or_done(it != this->end()) |
                   // we'll only evaluate the then sender if it !=
                   // this->end(), which means it's safe to
                   // dereference it
                   unifex::then(
                          [it]() noexcept -> decltype(auto) { return *it; });
             });
    });
  }

  auto cleanup() noexcept {
    return unifex::defer([this]() noexcept { return unifex::just_done(); });
  }

  void swap(async_generator& other) noexcept {
    using std::swap;
    swap(m_coroutine, other.m_coroutine);
  }

private:
  // My general feeling is let's just make those private & push just for a
  // stream-based processing approach. Within the implementation, it's still
  // convenient to deal with the iterator-based processing, though I also
  // wouldn't mind removing this altogether.
  // private:  <--
  auto begin() noexcept {
    if (!m_coroutine) {
      return detail::async_generator_begin_operation<T>{nullptr};
    }

    return detail::async_generator_begin_operation<T>{m_coroutine};
  }

  auto end() noexcept { return iterator{nullptr}; }

  unifex::coro::coroutine_handle<promise_type> m_coroutine;
  std::optional<iterator> it_;
};

namespace detail {
template <typename T>
async_generator<T> async_generator_promise<T>::get_return_object() noexcept {
  return async_generator<T>{*this};
}
}  // namespace detail

}  // namespace unifex

#endif

#include <unifex/detail/epilogue.hpp>
