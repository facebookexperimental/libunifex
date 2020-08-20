/*
 * Copyright 2019-present Facebook, Inc.
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
#include <unifex/manual_lifetime.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/type_list.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use this header"
#endif

#include <exception>
#include <optional>
#include <cassert>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _task {

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
  struct type;
};
template <typename T>
struct _task<T>::type {
  struct promise_type {
    void reset_value() noexcept {
      switch (std::exchange(state_, state::empty)) {
        case state::value:
          unifex::deactivate_union_member(value_);
          break;
        case state::exception:
          unifex::deactivate_union_member(exception_);
          break;
        default:
          break;
      }
    }

    type get_return_object() noexcept {
      return type{
          coro::coroutine_handle<promise_type>::from_promise(*this)};
    }

    coro::suspend_always initial_suspend() noexcept {
      return {};
    }

    auto final_suspend() noexcept {
      struct awaiter {
        bool await_ready() noexcept {
          return false;
        }
        auto await_suspend(
            coro::coroutine_handle<promise_type> h) noexcept {
          return h.promise().continuation_;
        }
        void await_resume() noexcept {}
      };
      return awaiter{};
    }

    void unhandled_exception() noexcept {
      reset_value();
      unifex::activate_union_member(exception_, std::current_exception());
      state_ = state::exception;
    }

    template(typename Value)
      (requires is_callable_v<decltype(unifex::await_transform), promise_type&, Value>)
    auto await_transform(Value&& value)
        noexcept(is_nothrow_callable_v<decltype(unifex::await_transform), promise_type&, Value>)
        -> callable_result_t<decltype(unifex::await_transform), promise_type&, Value> {
      return unifex::await_transform(*this, (Value&&)value);
    }

    template(typename Value)
        (requires convertible_to<Value, T>)
    void return_value(Value&& value) noexcept(
        std::is_nothrow_constructible_v<T, Value>) {
      reset_value();
      unifex::activate_union_member(value_, (Value &&) value);
      state_ = state::value;
    }

    promise_type() noexcept {}

    ~promise_type() {
      reset_value();
    }

    decltype(auto) result() {
      if (state_ == state::exception) {
        std::rethrow_exception(std::move(exception_).get());
      }
      return std::move(value_).get();
    }

    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const promise_type& p, Func&& func) {
      if (p.info_) {
        visit_continuations(*p.info_, (Func &&) func);
      }
    }

    friend inplace_stop_token tag_invoke(tag_t<get_stop_token>, const promise_type& p) noexcept {
      return p.stoken_;
    }

    enum class state { empty, value, exception };

    coro::coroutine_handle<> continuation_;
    state state_ = state::empty;
    union {
      manual_lifetime<T> value_;
      manual_lifetime<std::exception_ptr> exception_;
    };
    std::optional<continuation_info> info_;
    inplace_stop_token stoken_;
  };

  template<
    template<typename...> class Variant,
    template<typename...> class Tuple>
  using value_types = Variant<
    typename std::conditional_t<
      std::is_void_v<T>, type_list<>, type_list<T>>
    ::template apply<Tuple>>;

  template<
    template<typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = false;

  explicit type(coro::coroutine_handle<promise_type> h) noexcept
      : coro_(h) {}

  ~type() {
    if (coro_)
      coro_.destroy();
  }

  type(type&& t) noexcept : coro_(std::exchange(t.coro_, {})) {}

  type& operator=(type t) noexcept {
    std::swap(coro_, t.coro_);
    return *this;
  }

private:
  template<typename Promise>
  struct awaiter {
    explicit awaiter(coro::coroutine_handle<promise_type> coro) noexcept
    : coro_(coro)
    {}

    awaiter(awaiter&& other) noexcept
    : coro_(std::exchange(other.coro_, {}))
    {}

    ~awaiter() {
      if (coro_) coro_.destroy();
    }

    bool await_ready() noexcept {
      return false;
    }

    coro::coroutine_handle<promise_type> await_suspend(
        coro::coroutine_handle<Promise> h) noexcept {
      assert(coro_);
      coro_.promise().continuation_ = h;
      coro_.promise().info_.emplace(
          continuation_info::from_continuation(h.promise()));
      coro_.promise().stoken_ = stopTokenAdapter_.subscribe(get_stop_token(h.promise()));
      return coro_;
    }

    std::optional<non_void_t<wrap_reference_t<decay_rvalue_t<T>>>> await_resume() {
      stopTokenAdapter_.unsubscribe();
      scope_guard destroyOnExit{[this]() noexcept { std::exchange(coro_, {}).destroy(); }};
      return coro_.promise().result();
    }

  private:
    coro::coroutine_handle<promise_type> coro_;
    UNIFEX_NO_UNIQUE_ADDRESS inplace_stop_token_adapter<stop_token_type_t<Promise>> stopTokenAdapter_;
  };

  template<typename Promise>
  friend awaiter<Promise> tag_invoke(tag_t<unifex::await_transform>, Promise& promise, type&& t) noexcept {
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
