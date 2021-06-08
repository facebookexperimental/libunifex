/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/async_trace.hpp>
#include <unifex/await_transform.hpp>
#include <unifex/connect_awaitable.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/invoke.hpp>
#include <unifex/at_coroutine_exit.hpp>
#include <unifex/continuations.hpp>
#include <unifex/any_scheduler.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use this header"
#endif

#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _task {
using namespace _util;

template <typename... Types>
using _is_single_valued_tuple =
    std::bool_constant<1 >= sizeof...(Types)>;

template <typename... Types>
using _is_single_valued_variant =
    std::bool_constant<sizeof...(Types) == 1 && (Types::value &&...)>;

template <typename Sender>
UNIFEX_CONCEPT_FRAGMENT(     //
  _single_typed_sender_impl, //
    requires()(0) &&         //
    sender_traits<remove_cvref_t<Sender>>
      ::template value_types<
        _is_single_valued_variant,
        _is_single_valued_tuple>::value);

template <typename Sender>
UNIFEX_CONCEPT _single_typed_sender =
  typed_sender<Sender> && UNIFEX_FRAGMENT(_single_typed_sender_impl, Sender);

template <typename T>
struct _task {
  struct [[nodiscard]] type;
};

struct _promise_base {
  struct _final_suspend_awaiter_base {
    static bool await_ready() noexcept {
      return false;
    }
    static void await_resume() noexcept {}
  };

  coro::suspend_always initial_suspend() noexcept {
    return {};
  }

  coro::coroutine_handle<> unhandled_done() noexcept {
    return continuation_.done();
  }

  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const _promise_base& p, Func&& func) {
    visit_continuations(p.continuation_, (Func &&) func);
  }

  friend inplace_stop_token tag_invoke(tag_t<get_stop_token>, const _promise_base& p) noexcept {
    return p.stoken_;
  }

  friend any_scheduler_ref tag_invoke(tag_t<get_scheduler>, const _promise_base& p) noexcept {
    return p.sched_;
  }

  friend continuation_handle<> tag_invoke(
      tag_t<exchange_continuation>, _promise_base& p, continuation_handle<> action) noexcept {
    return std::exchange(p.continuation_, (continuation_handle<>&&) action);
  }

  inline static constexpr inline_scheduler _default_scheduler{};

  continuation_handle<> continuation_;
  inplace_stop_token stoken_;
  any_scheduler_ref sched_{_default_scheduler};
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
        auto await_suspend(coro::coroutine_handle<type> h) noexcept {
          return h.promise().continuation_.handle();
        }
      };
      return awaiter{};
    }

    void unhandled_exception() noexcept {
      this->expected_.reset_value();
      unifex::activate_union_member(this->expected_.exception_, std::current_exception());
      this->expected_.state_ = _state::exception;
    }

    template(typename Value)
        (requires callable<decltype(unifex::await_transform), type&, Value>)
    auto await_transform(Value&& value)
        noexcept(is_nothrow_callable_v<decltype(unifex::await_transform), type&, Value>)
        -> callable_result_t<decltype(unifex::await_transform), type&, Value> {
      return unifex::await_transform(*this, (Value&&)value);
    }

    decltype(auto) result() {
      if (this->expected_.state_ == _state::exception) {
        std::rethrow_exception(std::move(this->expected_.exception_).get());
      }
      return std::move(this->expected_.value_).get();
    }
  };
};

template<typename ThisPromise, typename OtherPromise>
struct _awaiter {
  struct type {
    using result_type = typename ThisPromise::result_type;

    explicit type(coro::coroutine_handle<ThisPromise> coro) noexcept
      : coro_((std::uintptr_t) coro.address())
    {}

    type(type&& other) noexcept = delete;

    ~type() {
      if (coro_ & 1u) {
        auto thisCoro = coro::coroutine_handle<>::from_address((void*) --coro_);
        thisCoro.destroy();
        stopTokenAdapter_.unsubscribe();
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
      sched_.construct(get_scheduler(h.promise()));
      promise.continuation_ = h;
      promise.stoken_ = stopTokenAdapter_.subscribe(get_stop_token(h.promise()));
      promise.sched_ = sched_.get();
      return thisCoro;
    }

    result_type await_resume() {
      stopTokenAdapter_.unsubscribe();
      sched_.destruct();
      auto thisCoro = coro::coroutine_handle<ThisPromise>::from_address(
          (void*) std::exchange(--coro_, 0));
      scope_guard destroyOnExit{[&]() noexcept { thisCoro.destroy(); }};
      return thisCoro.promise().result();
    }

  private:
    std::uintptr_t coro_; // Stored as an integer so we can use the low bit as a dirty bit
    manual_lifetime<get_scheduler_result_t<OtherPromise&>> sched_;
    UNIFEX_NO_UNIQUE_ADDRESS
    inplace_stop_token_adapter<stop_token_type_t<OtherPromise>> stopTokenAdapter_;
  };
};

template <typename T>
struct _task<T>::type {
  using promise_type = typename _promise<T>::type;
  friend promise_type;

  template<
    template<typename...> class Variant,
    template<typename...> class Tuple>
  using value_types =
    Variant<
      typename conditional_t<std::is_void_v<T>, type_list<>, type_list<T>>
        ::template apply<Tuple>>;

  template<
    template<typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  ~type() {
    if (coro_)
      coro_.destroy();
  }

  type(type&& t) noexcept
    : coro_(std::exchange(t.coro_, {})) {}

  type& operator=(type t) noexcept {
    std::swap(coro_, t.coro_);
    return *this;
  }

  template <typename Fn, typename... Args>
  friend type tag_invoke(
      tag_t<co_invoke>, type_identity<type>, Fn fn, Args... args) {
    co_return co_await std::invoke((Fn&&) fn, (Args&&) args...);
  }

private:
  template <typename OtherPromise>
  using awaiter = typename _awaiter<promise_type, OtherPromise>::type;

  explicit type(coro::coroutine_handle<promise_type> h) noexcept
    : coro_(h) {}

  template<typename Promise>
  friend awaiter<Promise> tag_invoke(tag_t<unifex::await_transform>, Promise&, type&& t) noexcept {
    return awaiter<Promise>{std::exchange(t.coro_, {})};
  }

  template<typename Receiver>
  friend auto tag_invoke(tag_t<unifex::connect>, type&& t, Receiver&& r) {
    return unifex::connect_awaitable((type&&)t, (Receiver&&)r);
  }

  coro::coroutine_handle<promise_type> coro_;
};

} // namespace _task

template <typename T>
using task = typename _task::_task<T>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
