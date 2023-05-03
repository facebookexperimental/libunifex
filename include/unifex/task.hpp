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

#include <unifex/any_scheduler.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/await_transform.hpp>
#include <unifex/blocking.hpp>
#include <unifex/connect_awaitable.hpp>
#include <unifex/continuations.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/finally.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/invoke.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use this header"
#endif

#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _task {
using namespace _util;

struct coro_holder {
  explicit coro_holder(coro::coroutine_handle<> h) noexcept
      : coro_(std::move(h)) {}

  coro_holder(coro_holder&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~coro_holder() {
    if (coro_) {
      coro_.destroy();
    }
  }

  coro_holder& operator=(coro_holder rhs) noexcept {
    std::swap(coro_, rhs.coro_);
    return *this;
  }

 protected:
  coro::coroutine_handle<> coro_;
};

template <typename T>
struct _task {
  struct [[nodiscard]] type;
};

struct _promise_base {
  struct _final_suspend_awaiter_base {
    bool await_ready() noexcept {
      return false;
    }
    void await_resume() noexcept {}

    friend constexpr auto tag_invoke(tag_t<unifex::blocking>, const _final_suspend_awaiter_base&) noexcept {
      return blocking_kind::always_inline;
    }
  };

  void transform_schedule_sender_impl_(any_scheduler newSched);

  coro::suspend_always initial_suspend() noexcept {
    return {};
  }

  coro::coroutine_handle<> unhandled_done() noexcept {
    return continuation_.done();
  }

#ifdef UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const _promise_base& p, Func&& func) {
    visit_continuations(p.continuation_, (Func &&) func);
  }
#endif

  friend inplace_stop_token tag_invoke(tag_t<get_stop_token>, const _promise_base& p) noexcept {
    return p.stoken_;
  }

  friend any_scheduler tag_invoke(tag_t<get_scheduler>, const _promise_base& p) noexcept {
    return p.sched_;
  }

  friend continuation_handle<> tag_invoke(
      const tag_t<exchange_continuation>&, _promise_base& p, continuation_handle<> action) noexcept {
    return std::exchange(p.continuation_, (continuation_handle<>&&) action);
  }

  inline static constexpr inline_scheduler _default_scheduler{};

  continuation_handle<> continuation_;
  inplace_stop_token stoken_;
  any_scheduler sched_{_default_scheduler};
  bool rescheduled_ = false;
};

template <typename T>
struct _return_value_or_void {
  struct type {
    template(typename Value = T)
        (requires convertible_to<Value, T> AND constructible_from<T, Value>)
    void return_value(Value&& value) noexcept(
        std::is_nothrow_constructible_v<T, Value>) {
      expected_.reset_value();
      unifex::activate_union_member(expected_.value_, (Value &&) value);
      expected_.state_ = _state::value;
    }
    _expected<T> expected_;
  };
};

template <>
struct _return_value_or_void<void> {
  struct type {
    void return_void() noexcept {
      expected_.reset_value();
      unifex::activate_union_member(expected_.value_);
      expected_.state_ = _state::value;
    }
    _expected<void> expected_;
  };
};

struct _task_base {};

template <typename T>
struct _promise {
  struct type : _promise_base, _return_value_or_void<T>::type {
    using result_type = T;

    typename _task<T>::type get_return_object() noexcept {
      return typename _task<T>::type{
          coro::coroutine_handle<type>::from_promise(*this)};
    }

    auto final_suspend() noexcept {
      struct awaiter : _final_suspend_awaiter_base {
#if (defined(_MSC_VER) && !defined(__clang__)) || defined(__EMSCRIPTEN__)
        // MSVC doesn't seem to like symmetric transfer in this final awaiter and
        // the Emscripten (WebAssembly) compiler doesn't support tail-calls
        void await_suspend(coro::coroutine_handle<type> h) noexcept {
          return h.promise().continuation_.handle().resume();
        }
#else
        auto await_suspend(coro::coroutine_handle<type> h) noexcept {
          return h.promise().continuation_.handle();
        }
#endif
      };
      return awaiter{};
    }

    void unhandled_exception() noexcept {
      this->expected_.reset_value();
      unifex::activate_union_member(this->expected_.exception_, std::current_exception());
      this->expected_.state_ = _state::exception;
    }

    template <typename Value>
    decltype(auto) await_transform(Value&& value) {
      if constexpr (derived_from<remove_cvref_t<Value>, _task_base>) {
        // We are co_await-ing a unifex::task, which completes inline because of task
        // scheduler affinity. We don't need an additional transition.
        return unifex::await_transform(*this, (Value&&) value);
      } else if constexpr (tag_invocable<tag_t<unifex::await_transform>, type&, Value>
          || detail::_awaitable<Value>) {
        // Either await_transform has been customized or Value is an awaitable. Either
        // way, we can dispatch to the await_transform CPO, then insert a transition back
        // to the correct execution context if necessary.
        return transform_awaitable_(unifex::await_transform(*this, (Value&&) value));
      } else if constexpr (unifex::sender<Value>) {
        return transform_sender_((Value&&) value);
      } else {
        // Otherwise, we don't know how to await this type. Just return it and let the
        // compiler issue a diagnostic.
        return (Value&&) value;
      }
    }

    template <typename Awaitable>
    decltype(auto) transform_awaitable_(Awaitable&& awaitable) {
      if constexpr (blocking_kind::always_inline == cblocking<Awaitable>()) {
        return Awaitable{(Awaitable&&) awaitable};
      } else {
        return unifex::await_transform(
            *this,
            finally(as_sender((Awaitable&&) awaitable), unstoppable(schedule(this->sched_))));
      }
    }

    template <typename Sender>
    decltype(auto) transform_sender_(Sender&& sndr) {
      if constexpr (blocking_kind::always_inline == cblocking<Sender>()) {
        return unifex::await_transform(*this, (Sender&&) sndr);
      } else if constexpr (is_sender_for_v<remove_cvref_t<Sender>, schedule>) {
        // If we are co_await'ing a sender that is the result of calling schedule,
        // do something special
        return transform_schedule_sender_((Sender&&) sndr);
      } else {
        // Otherwise, append a transition to the correct execution context and wrap the
        // result in an awaiter:
        return unifex::await_transform(*this,
            finally((Sender&&) sndr, unstoppable(schedule(this->sched_))));
      }
    }

    // co_await schedule(sched) is magical. It does the following:
    // - transitions execution context
    // - updates the coroutine's current scheduler
    // - schedules an async cleanup action that transitions back to the correct
    //   context at the end of the coroutine (if one has not already been scheduled).
    template <typename ScheduleSender>
    decltype(auto) transform_schedule_sender_(ScheduleSender&& snd) {
      // This sender is a scheduler provider. Get the scheduler. This get_scheduler
      // call returns a reference to the scheduler stored within snd, which is an object
      // whose lifetime spans a suspend point. So it's ok to build an any_scheduler_ref
      // from it:
      transform_schedule_sender_impl_(get_scheduler(snd));

      // Return the inner sender, appropriately wrapped in an awaitable:
      return unifex::await_transform(*this, std::move(snd).base());
    }

    decltype(auto) result() {
      if (this->expected_.state_ == _state::exception) {
        std::rethrow_exception(std::move(this->expected_.exception_).get());
      }
      return std::move(this->expected_.value_).get();
    }
  };
};

struct tagged_coro_holder {
  explicit tagged_coro_holder(coro::coroutine_handle<> h) noexcept
      : coro_((std::uintptr_t) h.address()) {
    UNIFEX_ASSERT(coro_);
  }

  tagged_coro_holder(tagged_coro_holder&& other) noexcept
      : coro_(std::exchange(other.coro_, 0)) {
      UNIFEX_ASSERT(coro_ && ((coro_ & 1u) == 0u));
  }

  ~tagged_coro_holder() {
    static constexpr std::uintptr_t mask = ~(std::uintptr_t{1u});

    if ((coro_ & mask) != 0u) {
      auto address = reinterpret_cast<void*>(coro_ & mask);
      coro::coroutine_handle<>::from_address(address).destroy();
    }
  }

 protected:
  // Stored as an integer so we can use the low bit as a dirty bit
  std::uintptr_t coro_;
};

template<typename ThisPromise, typename OtherPromise>
struct _awaiter {
  struct type : tagged_coro_holder {
    using result_type = typename ThisPromise::result_type;

    explicit type(coro::coroutine_handle<> coro) noexcept
      : tagged_coro_holder(coro) {}

    // The move constructor is only ever called /before/ the awaitable is awaited.
    // In those cases, the other fields have not been initialized yet and so do not
    // need to be moved.
    type(type&& other) noexcept
      : tagged_coro_holder(std::move(other)) {}

    ~type() {
      if (coro_ & 1u) {
        if constexpr (needs_stop_token_t::value)
          stopTokenAdapter_.unsubscribe();
        if constexpr (needs_scheduler_t::value)
          sched_.destruct();
      }
    }

    bool await_ready() noexcept {
      return false;
    }

    coro::coroutine_handle<ThisPromise> await_suspend(
        coro::coroutine_handle<OtherPromise> h) noexcept {
      UNIFEX_ASSERT(coro_ && ((coro_ & 1u) == 0u));
      auto thisCoro = coro::coroutine_handle<ThisPromise>::from_address((void*) coro_);
      ++coro_; // mark the awaiter as needing cleanup
      auto& promise = thisCoro.promise();
      promise.continuation_ = h;
      if constexpr (needs_scheduler_t::value) {
        sched_.construct(get_scheduler(h.promise()));
        promise.sched_ = sched_.get();
      } else {
        promise.sched_ = get_scheduler(h.promise());
      }
      if constexpr (needs_stop_token_t::value) {
        promise.stoken_ = stopTokenAdapter_.subscribe(get_stop_token(h.promise()));
      } else {
        promise.stoken_ = get_stop_token(h.promise());
      }
      return thisCoro;
    }

    result_type await_resume() {
      if constexpr (needs_stop_token_t::value)
        stopTokenAdapter_.unsubscribe();
      if constexpr (needs_scheduler_t::value)
        sched_.destruct();
      auto thisCoro = coro::coroutine_handle<ThisPromise>::from_address(
          (void*) std::exchange(--coro_, 0));
      coro_holder destroyOnExit{thisCoro};
      return thisCoro.promise().result();
    }

  private:
    using scheduler_t = remove_cvref_t<get_scheduler_result_t<OtherPromise&>>;
    using stop_token_t = remove_cvref_t<stop_token_type_t<OtherPromise>>;
    using needs_scheduler_t =
        std::bool_constant<!same_as<scheduler_t, any_scheduler>>;
    using needs_stop_token_t =
        std::bool_constant<!same_as<stop_token_t, inplace_stop_token>>;

    // Only store the scheduler and the stop_token in the awaiter if we need to type
    // erase them. Otherwise, these members are "empty" and should take up no space
    // because of the [[no_unique_address]] attribute.
    // Note: for the compiler to fold the members away, they must have different types.
    // Hence, the slightly odd-looking template parameter to the empty struct.
    UNIFEX_NO_UNIQUE_ADDRESS
    conditional_t<
        needs_scheduler_t::value,
        manual_lifetime<scheduler_t>,
        detail::_empty<0>> sched_;
    UNIFEX_NO_UNIQUE_ADDRESS
    conditional_t<
        needs_stop_token_t::value,
        inplace_stop_token_adapter<stop_token_t>,
        detail::_empty<1>> stopTokenAdapter_;
  };
};

template <typename T>
struct _task<T>::type : _task_base, coro_holder {
  using promise_type = typename _promise<T>::type;
  friend promise_type;

  template<
    template<typename...> class Variant,
    template<typename...> class Tuple>
  using value_types =
    Variant<
      typename std::conditional_t<std::is_void_v<T>, type_list<>, type_list<T>>
        ::template apply<Tuple>>;

  template<
    template<typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  type(type&& t) noexcept = default;

  type& operator=(type&& t) noexcept = default;

  template <typename Fn, typename... Args>
  friend type tag_invoke(
      tag_t<co_invoke>, type_identity<type>, Fn fn, Args... args) {
    co_return co_await std::invoke((Fn&&) fn, (Args&&) args...);
  }

private:
  template <typename OtherPromise>
  using awaiter = typename _awaiter<promise_type, OtherPromise>::type;

  explicit type(coro::coroutine_handle<promise_type> h) noexcept
    : coro_holder(h) {}

  template<typename Promise>
  friend awaiter<Promise> tag_invoke(tag_t<unifex::await_transform>, Promise&, type&& t) noexcept {
    return awaiter<Promise>{std::exchange(t.coro_, {})};
  }

  template<typename Receiver>
  friend auto tag_invoke(tag_t<unifex::connect>, type&& t, Receiver&& r) {
    return unifex::connect_awaitable((type&&) t, (Receiver&&) r);
  }
};

} // namespace _task

template <typename T>
using task = typename _task::_task<T>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
